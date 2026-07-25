/* ============================================================
 * BotOS Core — BotNotify client library
 * ============================================================
 * File:    bot_notify.c
 * License: MIT
 * ============================================================ */

#include "bot_notify.h"

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

int bot_notify_send(const char *title, const char *body)
{
    if (!title) title = "";
    if (!body)  body  = "";
    if (strchr(title, '|') || strchr(body, '|')) {
        /* The wire format uses '|' as the only separator (see
         * bot_notify.h) — reject rather than silently mangle the
         * message into the wrong title/body split. */
        errno = EINVAL;
        return -1;
    }

    char msg[BOT_NOTIFY_MAX_LEN];
    int n = snprintf(msg, sizeof(msg), "%s|%s", title, body);
    if (n < 0) return -1;
    if ((size_t)n >= sizeof(msg)) n = (int)sizeof(msg) - 1; /* truncate, don't fail */

    /* Create the FIFO if this is the first notification ever sent —
     * whichever process (BotDesk or otherwise) gets here first is
     * fine, mkfifo() on an existing path just fails harmlessly. */
    mkfifo(BOT_NOTIFY_FIFO_PATH, 0666);

    /* O_NONBLOCK on open(): if nobody has the read end open (BotDesk
     * isn't running, or isn't listening yet), this fails immediately
     * with ENXIO instead of hanging the caller waiting for a reader. */
    int fd = open(BOT_NOTIFY_FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;

    ssize_t written = write(fd, msg, (size_t)n);
    close(fd);

    return (written == n) ? 0 : -1;
}
