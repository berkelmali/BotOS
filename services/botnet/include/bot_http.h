/* ============================================================
 * BotOS Core — HTTP Client API
 * ============================================================
 * File:    bot_http.h
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_HTTP_H
#define BOTOS_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** HTTP response structure. */
typedef struct bot_http_response {
    int      status_code;    /**< HTTP status (200, 404, etc). */
    char    *body;           /**< Response body (heap alloc).  */
    size_t   body_size;      /**< Body length in bytes.        */
    char    *content_type;   /**< Content-Type header value.   */
} bot_http_response_t;

/**
 * Perform an HTTP GET request.
 * @param url       Full URL to request.
 * @param response  Output: response data.
 * @return          0 on success, -1 on error.
 */
int bot_http_get(const char *url, bot_http_response_t *response);

/**
 * Perform an HTTP POST request.
 * @param url       Full URL to request.
 * @param body      Request body.
 * @param body_len  Body length.
 * @param response  Output: response data.
 * @return          0 on success, -1 on error.
 */
int bot_http_post(const char *url, const void *body, size_t body_len,
                  bot_http_response_t *response);

/**
 * Download a file from URL to local path.
 * @param url       Remote URL.
 * @param path      Local file path.
 * @return          0 on success, -1 on error.
 */
int bot_http_download(const char *url, const char *path);

/** Free response resources. */
void bot_http_response_free(bot_http_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_HTTP_H */
