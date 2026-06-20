/* ============================================================
 * BotOS Core — HTTP Client (Production)
 * ============================================================
 * File:    http.c
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — TLS/HTTPS support using OpenSSL.
 * ============================================================ */

#include "bot_http.h"
#include "bot_tcp.h"
#include "bot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <pthread.h>

/* ── Constants ───────────────────────────────────────────── */

#define HTTP_MAX_REDIRECTS     5
#define HTTP_INITIAL_BUF_SIZE  8192
#define HTTP_MAX_RESPONSE_SIZE (16 * 1024 * 1024)  /* 16 MB */
#define HTTP_USER_AGENT        "BotNet/0.4.0"
#define HTTP_TIMEOUT_SEC       30

/* ── OpenSSL Lifecycle State ─────────────────────────────── */

static int g_ssl_initialized = 0;
static pthread_mutex_t g_ssl_init_mutex = PTHREAD_MUTEX_INITIALIZER;

static void init_openssl(void)
{
    pthread_mutex_lock(&g_ssl_init_mutex);
    if (!g_ssl_initialized) {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        g_ssl_initialized = 1;
    }
    pthread_mutex_unlock(&g_ssl_init_mutex);
}

/* ── Internal: Parsed URL ────────────────────────────────── */

typedef struct {
    char host[256];
    char path[2048];
    int  port;
    int  is_https;
} parsed_url_t;

/**
 * Parse a URL into components.
 * Supports: http://host:port/path, http://host/path, http://host
 */
static int parse_url(const char *url, parsed_url_t *out)
{
    if (!url || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->port = 80;

    const char *p = url;

    /* Parse scheme */
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        out->port = 443;
        out->is_https = 1;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    /* Parse host and optional port */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        /* host:port */
        size_t hlen = (size_t)(colon - p);
        if (hlen >= sizeof(out->host)) hlen = sizeof(out->host) - 1;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        out->port = atoi(colon + 1);
        if (out->port <= 0 || out->port > 65535) out->port = 80;
        p = slash ? slash : p + strlen(p);
    } else if (slash) {
        /* host/path */
        size_t hlen = (size_t)(slash - p);
        if (hlen >= sizeof(out->host)) hlen = sizeof(out->host) - 1;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        p = slash;
    } else {
        /* host only */
        strncpy(out->host, p, sizeof(out->host) - 1);
        p = "";
    }

    /* Parse path (default to /) */
    if (p[0] == '\0') {
        strcpy(out->path, "/");
    } else {
        strncpy(out->path, p, sizeof(out->path) - 1);
    }

    if (out->host[0] == '\0') return -1;

    return 0;
}

/* ── Internal: Response Buffer ───────────────────────────── */

typedef struct {
    char  *data;
    size_t size;
    size_t capacity;
} response_buf_t;

static int rbuf_init(response_buf_t *rb)
{
    rb->capacity = HTTP_INITIAL_BUF_SIZE;
    rb->size     = 0;
    rb->data     = (char *)malloc(rb->capacity);
    return rb->data ? 0 : -1;
}

static int rbuf_append(response_buf_t *rb, const char *data, size_t len)
{
    while (rb->size + len >= rb->capacity) {
        if (rb->capacity >= HTTP_MAX_RESPONSE_SIZE) {
            return -1;  /* Response too large */
        }
        rb->capacity *= 2;
        if (rb->capacity > HTTP_MAX_RESPONSE_SIZE) {
            rb->capacity = HTTP_MAX_RESPONSE_SIZE;
        }
        char *new_data = (char *)realloc(rb->data, rb->capacity);
        if (!new_data) return -1;
        rb->data = new_data;
    }

    memcpy(rb->data + rb->size, data, len);
    rb->size += len;
    rb->data[rb->size] = '\0';

    return 0;
}

static void rbuf_free(response_buf_t *rb)
{
    free(rb->data);
    rb->data = NULL;
    rb->size = 0;
    rb->capacity = 0;
}

/* ── Internal: Header Parsing ────────────────────────────── */

/**
 * Extract a header value from raw response data.
 * Case-insensitive key matching.
 */
static int http_get_header(const char *headers, const char *key,
                           char *out, size_t out_size)
{
    if (!headers || !key || !out) return -1;

    size_t key_len = strlen(key);
    const char *line = headers;

    while (line && *line) {
        /* Case-insensitive compare */
        if (strncasecmp(line, key, key_len) == 0 && line[key_len] == ':') {
            const char *val = line + key_len + 1;
            while (*val == ' ') val++;

            /* Find end of line */
            const char *eol = strstr(val, "\r\n");
            size_t vlen = eol ? (size_t)(eol - val) : strlen(val);
            if (vlen >= out_size) vlen = out_size - 1;

            memcpy(out, val, vlen);
            out[vlen] = '\0';
            return 0;
        }

        /* Advance to next line */
        const char *next = strstr(line, "\r\n");
        if (!next) break;
        line = next + 2;
    }

    return -1;
}

/**
 * Parse the HTTP status code from the response status line.
 */
static int http_parse_status(const char *response)
{
    if (!response) return -1;

    /* Format: HTTP/1.x STATUS_CODE REASON */
    const char *sp = strchr(response, ' ');
    if (!sp) return -1;

    return atoi(sp + 1);
}

/* ── Internal: Full Response Read ────────────────────────── */

static int http_read_response(int sockfd, SSL *ssl, bot_http_response_t *response)
{
    response_buf_t rb;
    if (rbuf_init(&rb) != 0) return -1;

    /* Read all data from the socket or SSL */
    char chunk[4096];
    ssize_t n;

    while (1) {
        if (ssl) {
            n = SSL_read(ssl, chunk, sizeof(chunk));
        } else {
            n = bot_tcp_recv(sockfd, chunk, sizeof(chunk));
        }

        if (n <= 0) {
            if (ssl) {
                int ssl_err = SSL_get_error(ssl, (int)n);
                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    continue;
                }
            }
            break;
        }

        if (rbuf_append(&rb, chunk, (size_t)n) != 0) {
            rbuf_free(&rb);
            return -1;
        }
    }

    if (rb.size == 0) {
        rbuf_free(&rb);
        return -1;
    }

    /* Parse status code */
    response->status_code = http_parse_status(rb.data);

    /* Find header/body boundary */
    const char *body_sep = strstr(rb.data, "\r\n\r\n");
    if (!body_sep) {
        /* No proper header termination */
        rbuf_free(&rb);
        return -1;
    }

    size_t header_len = (size_t)(body_sep - rb.data);
    const char *body_start = body_sep + 4;
    size_t body_len = rb.size - (size_t)(body_start - rb.data);

    /* Extract Content-Type */
    char ct_buf[256];
    if (http_get_header(rb.data, "Content-Type", ct_buf, sizeof(ct_buf)) == 0) {
        response->content_type = strdup(ct_buf);
    }

    /* Copy body */
    if (body_len > 0) {
        response->body = (char *)malloc(body_len + 1);
        if (response->body) {
            memcpy(response->body, body_start, body_len);
            response->body[body_len] = '\0';
            response->body_size = body_len;
        }
    }

    /* Check for redirect — extract Location header */
    if (response->status_code == 301 || response->status_code == 302 ||
        response->status_code == 307 || response->status_code == 308) {
        /* Null-terminate headers for extraction */
        char *headers_copy = (char *)malloc(header_len + 1);
        if (headers_copy) {
            memcpy(headers_copy, rb.data, header_len);
            headers_copy[header_len] = '\0';

            char location[2048];
            if (http_get_header(headers_copy, "Location", location, sizeof(location)) == 0) {
                /* Store redirect URL in content_type temporarily
                 * (the caller will handle the redirect) */
                free(response->content_type);
                response->content_type = strdup(location);
            }
            free(headers_copy);
        }
    }

    rbuf_free(&rb);
    return 0;
}

/* ── Internal: Perform Raw HTTP/HTTPS Request ────────────── */

static int http_request(const char *method, const char *url,
                        const void *body, size_t body_len,
                        const char *content_type,
                        bot_http_response_t *response,
                        int redirect_count)
{
    if (redirect_count > HTTP_MAX_REDIRECTS) {
        BOT_LOG_ERROR("Too many redirects (max %d)", HTTP_MAX_REDIRECTS);
        return -1;
    }

    parsed_url_t pu;
    if (parse_url(url, &pu) != 0) {
        BOT_LOG_ERROR("Invalid URL: %s", url);
        return -1;
    }

    /* Connect */
    int sockfd = bot_tcp_connect(pu.host, pu.port);
    if (sockfd < 0) return -1;

    bot_tcp_set_timeout(sockfd, HTTP_TIMEOUT_SEC);

    SSL_CTX *ssl_ctx = NULL;
    SSL *ssl = NULL;

    if (pu.is_https) {
        init_openssl();
        const SSL_METHOD *ssl_method = TLS_client_method();
        ssl_ctx = SSL_CTX_new(ssl_method);
        if (!ssl_ctx) {
            BOT_LOG_ERROR("Failed to create SSL context");
            bot_tcp_close(sockfd);
            return -1;
        }

        /* Enable default CA paths and verify peer certificate */
        SSL_CTX_set_default_verify_paths(ssl_ctx);
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);

        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            BOT_LOG_ERROR("Failed to create SSL structure");
            SSL_CTX_free(ssl_ctx);
            bot_tcp_close(sockfd);
            return -1;
        }

        /* Set SNI host name */
        SSL_set_tlsext_host_name(ssl, pu.host);

        /* Set hostname check for verification */
        X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        X509_VERIFY_PARAM_set1_host(param, pu.host, 0);

        SSL_set_fd(ssl, sockfd);

        if (SSL_connect(ssl) <= 0) {
            BOT_LOG_ERROR("SSL connection failed");
            unsigned long err = ERR_get_error();
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            BOT_LOG_ERROR("OpenSSL error: %s", err_buf);

            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            bot_tcp_close(sockfd);
            return -1;
        }

        BOT_LOG_INFO("SSL connection established with %s using %s",
                     pu.host, SSL_get_cipher(ssl));
    }

    /* Build request */
    char header[4096];
    int hlen;

    if (body && body_len > 0) {
        hlen = snprintf(header, sizeof(header),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "Content-Length: %zu\r\n"
            "Content-Type: %s\r\n"
            "Connection: close\r\n"
            "Accept: */*\r\n"
            "\r\n",
            method, pu.path, pu.host, HTTP_USER_AGENT,
            body_len,
            content_type ? content_type : "application/octet-stream");
    } else {
        hlen = snprintf(header, sizeof(header),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "Connection: close\r\n"
            "Accept: */*\r\n"
            "\r\n",
            method, pu.path, pu.host, HTTP_USER_AGENT);
    }

    int request_failed = 0;

    /* Send request */
    if (ssl) {
        if (SSL_write(ssl, header, hlen) <= 0) {
            request_failed = 1;
        }
    } else {
        if (bot_tcp_send(sockfd, header, (size_t)hlen) < 0) {
            request_failed = 1;
        }
    }

    /* Send body if present */
    if (!request_failed && body && body_len > 0) {
        if (ssl) {
            if (SSL_write(ssl, body, (int)body_len) <= 0) {
                request_failed = 1;
            }
        } else {
            if (bot_tcp_send(sockfd, body, body_len) < 0) {
                request_failed = 1;
            }
        }
    }

    if (request_failed) {
        if (ssl) SSL_free(ssl);
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
        bot_tcp_close(sockfd);
        return -1;
    }

    /* Read response */
    memset(response, 0, sizeof(*response));
    int ret = http_read_response(sockfd, ssl, response);

    /* Shutdown SSL gracefully */
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
    }
    bot_tcp_close(sockfd);

    if (ret != 0) return -1;

    /* Handle redirects */
    if ((response->status_code == 301 || response->status_code == 302 ||
         response->status_code == 307 || response->status_code == 308) &&
        response->content_type) {

        char *redirect_url = strdup(response->content_type);
        BOT_LOG_DEBUG("HTTP %d redirect → %s", response->status_code, redirect_url);

        bot_http_response_free(response);

        ret = http_request(method, redirect_url, body, body_len,
                           content_type, response, redirect_count + 1);
        free(redirect_url);
        return ret;
    }

    return 0;
}

/* ── Public API ──────────────────────────────────────────── */

int bot_http_get(const char *url, bot_http_response_t *response)
{
    if (!url || !response) {
        errno = EINVAL;
        return -1;
    }

    memset(response, 0, sizeof(*response));
    int ret = http_request("GET", url, NULL, 0, NULL, response, 0);

    BOT_LOG_DEBUG("HTTP GET %s → %d (%zu bytes)",
                  url, response->status_code, response->body_size);
    return ret;
}

int bot_http_post(const char *url, const void *body, size_t body_len,
                  bot_http_response_t *response)
{
    if (!url || !response) {
        errno = EINVAL;
        return -1;
    }

    memset(response, 0, sizeof(*response));
    int ret = http_request("POST", url, body, body_len,
                           "application/octet-stream", response, 0);

    BOT_LOG_DEBUG("HTTP POST %s → %d (%zu bytes)",
                  url, response->status_code, response->body_size);
    return ret;
}

int bot_http_download(const char *url, const char *path)
{
    if (!url || !path) {
        errno = EINVAL;
        return -1;
    }

    bot_http_response_t resp;
    if (bot_http_get(url, &resp) != 0) return -1;

    if (resp.status_code != 200) {
        BOT_LOG_ERROR("Download failed: HTTP %d for %s", resp.status_code, url);
        bot_http_response_free(&resp);
        return -1;
    }

    if (!resp.body || resp.body_size == 0) {
        BOT_LOG_ERROR("Download failed: empty response for %s", url);
        bot_http_response_free(&resp);
        return -1;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        BOT_LOG_ERROR("Cannot open %s for writing: %s", path, strerror(errno));
        bot_http_response_free(&resp);
        return -1;
    }

    size_t written = fwrite(resp.body, 1, resp.body_size, fp);
    fclose(fp);

    if (written != resp.body_size) {
        BOT_LOG_ERROR("Write incomplete: %zu/%zu bytes", written, resp.body_size);
        bot_http_response_free(&resp);
        return -1;
    }

    bot_http_response_free(&resp);
    BOT_LOG_INFO("Downloaded %s → %s (%zu bytes)", url, path, written);
    return 0;
}

void bot_http_response_free(bot_http_response_t *response)
{
    if (!response) return;
    free(response->body);
    free(response->content_type);
    memset(response, 0, sizeof(*response));
}
