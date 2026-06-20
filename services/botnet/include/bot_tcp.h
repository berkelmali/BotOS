/* ============================================================
 * BotOS Core — TCP Socket API
 * ============================================================
 * File:    bot_tcp.h
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_TCP_H
#define BOTOS_TCP_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Connect to a remote TCP host.
 * @param host     Hostname or IP address.
 * @param port     Port number.
 * @return         Socket fd on success, -1 on error.
 */
int bot_tcp_connect(const char *host, int port);

/**
 * Send data over a TCP connection.
 * @param sockfd   Socket fd from bot_tcp_connect.
 * @param buf      Data to send.
 * @param len      Data length.
 * @return         Bytes sent, -1 on error.
 */
ssize_t bot_tcp_send(int sockfd, const void *buf, size_t len);

/**
 * Receive data from a TCP connection.
 * @param sockfd   Socket fd.
 * @param buf      Buffer to receive into.
 * @param buf_size Buffer capacity.
 * @return         Bytes received, 0 on disconnect, -1 on error.
 */
ssize_t bot_tcp_recv(int sockfd, void *buf, size_t buf_size);

/**
 * Close a TCP connection.
 * @param sockfd   Socket fd.
 */
void bot_tcp_close(int sockfd);

/**
 * Set socket timeout.
 * @param sockfd       Socket fd.
 * @param timeout_sec  Timeout in seconds.
 * @return             0 on success, -1 on error.
 */
int bot_tcp_set_timeout(int sockfd, int timeout_sec);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_TCP_H */
