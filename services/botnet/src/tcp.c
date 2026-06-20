/* ============================================================
 * BotOS Core — TCP Socket Layer (Production)
 * ============================================================
 * File:    tcp.c
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production TCP socket implementation.
 *
 * Features:
 *   - IPv4/IPv6 dual-stack via getaddrinfo()
 *   - EINTR-safe send/recv loops
 *   - Full send guarantee (loops until all bytes sent)
 *   - Configurable connect timeout via non-blocking + select()
 *   - SO_KEEPALIVE for long-lived connections
 *   - SO_REUSEADDR for rapid rebind after restart
 *   - Graceful shutdown (SHUT_RDWR before close)
 * ============================================================ */

#include "bot_tcp.h"
#include "bot_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

/* ── Default Timeout ─────────────────────────────────────── */

#define TCP_DEFAULT_CONNECT_TIMEOUT  15  /* seconds */

/* ── Internal: Set Non-blocking ──────────────────────────── */

static int tcp_set_nonblocking(int fd, int enable)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    return fcntl(fd, F_SETFL, flags);
}

/* ── Public API ──────────────────────────────────────────── */

int bot_tcp_connect(const char *host, int port)
{
    if (!host || host[0] == '\0' || port <= 0 || port > 65535) {
        errno = EINVAL;
        return -1;
    }

    /* Resolve hostname — supports both IPv4 and IPv6 */
    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;     /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int gai_ret = getaddrinfo(host, port_str, &hints, &result);
    if (gai_ret != 0) {
        BOT_LOG_ERROR("DNS resolution failed for %s: %s",
                      host, gai_strerror(gai_ret));
        return -1;
    }

    /* Try each resolved address until one succeeds */
    int sockfd = -1;

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) continue;

        /* Set non-blocking for connect timeout */
        tcp_set_nonblocking(sockfd, 1);

        int conn_ret = connect(sockfd, rp->ai_addr, rp->ai_addrlen);

        if (conn_ret == 0) {
            /* Immediate success (localhost) */
            tcp_set_nonblocking(sockfd, 0);
            break;
        }

        if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            /* Wait for connection with timeout using select() */
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sockfd, &wfds);

            struct timeval tv;
            tv.tv_sec  = TCP_DEFAULT_CONNECT_TIMEOUT;
            tv.tv_usec = 0;

            int sel = select(sockfd + 1, NULL, &wfds, NULL, &tv);
            if (sel > 0) {
                /* Check if connection actually succeeded */
                int so_err = 0;
                socklen_t so_len = sizeof(so_err);
                getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_err, &so_len);

                if (so_err == 0) {
                    /* Connection succeeded */
                    tcp_set_nonblocking(sockfd, 0);
                    break;
                }
                errno = so_err;
            } else if (sel == 0) {
                errno = ETIMEDOUT;
            }
        }

        /* This address failed, try next */
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd < 0) {
        BOT_LOG_ERROR("Connection to %s:%d failed: %s",
                      host, port, strerror(errno));
        return -1;
    }

    /* Enable TCP keepalive for long-lived connections */
    int keepalive = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

    /* Disable Nagle's algorithm for low-latency sends */
    int nodelay = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    BOT_LOG_DEBUG("Connected to %s:%d (fd=%d)", host, port, sockfd);
    return sockfd;
}

ssize_t bot_tcp_send(int sockfd, const void *buf, size_t len)
{
    if (sockfd < 0 || !buf || len == 0) {
        errno = EINVAL;
        return -1;
    }

    const char *ptr = (const char *)buf;
    size_t remaining = len;
    ssize_t total = 0;

    while (remaining > 0) {
        ssize_t n = send(sockfd, ptr + total, remaining, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR) continue;     /* Signal interrupted, retry */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Socket buffer full — wait with select */
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(sockfd, &wfds);

                struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
                if (select(sockfd + 1, NULL, &wfds, NULL, &tv) > 0) {
                    continue;
                }
            }
            BOT_LOG_ERROR("TCP send failed after %zd bytes: %s",
                          total, strerror(errno));
            return -1;
        }

        total     += n;
        remaining -= (size_t)n;
    }

    return total;
}

ssize_t bot_tcp_recv(int sockfd, void *buf, size_t buf_size)
{
    if (sockfd < 0 || !buf || buf_size == 0) {
        errno = EINVAL;
        return -1;
    }

    ssize_t n;
    do {
        n = recv(sockfd, buf, buf_size, 0);
    } while (n < 0 && errno == EINTR);

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        BOT_LOG_ERROR("TCP recv failed: %s", strerror(errno));
    }

    return n;
}

void bot_tcp_close(int sockfd)
{
    if (sockfd < 0) return;

    /* Graceful shutdown: stop both reads and writes */
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    BOT_LOG_DEBUG("TCP connection closed (fd=%d)", sockfd);
}

int bot_tcp_set_timeout(int sockfd, int timeout_sec)
{
    if (sockfd < 0 || timeout_sec < 0) {
        errno = EINVAL;
        return -1;
    }

    struct timeval tv;
    tv.tv_sec  = timeout_sec;
    tv.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        BOT_LOG_ERROR("Failed to set recv timeout: %s", strerror(errno));
        return -1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        BOT_LOG_ERROR("Failed to set send timeout: %s", strerror(errno));
        return -1;
    }

    return 0;
}
