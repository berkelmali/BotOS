/* ============================================================
 * BotOS Core — BotNotify (Desktop Notifications)
 * ============================================================
 * File:    bot_notify.h
 * Layer:   L5 — Platform Services
 * License: MIT
 *
 * A minimal, dependency-free desktop notification channel: any
 * process (a shell script, botpkg, a background job) can pop up a
 * small toast in the corner of the screen without linking against
 * botui or talking to X11 directly.
 *
 * Transport: a well-known named FIFO. This is deliberately NOT built
 * on bot_ipc's bot_mq_* message queues — those are process-local only
 * (see the comment above g_mq_table in core/ipc/src/ipc.c), so two
 * separate processes never actually see the same queue; a FIFO is a
 * genuine, simple, kernel-mediated cross-process channel and needs no
 * changes to bot_ipc's internals to get that.
 *
 * Wire format: exactly one write() call per notification, containing
 * "title|body" (a single '|' separates the two; notifications can't
 * currently contain a literal '|' themselves — a fine limitation for
 * short toast text, and simple enough that a single small write is
 * atomic on any pipe/FIFO, so readers never see a torn message).
 * ============================================================ */

#ifndef BOTOS_NOTIFY_H
#define BOTOS_NOTIFY_H

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_NOTIFY_FIFO_PATH "/tmp/.botos_notify"
#define BOT_NOTIFY_MAX_LEN   256  /**< Max bytes for "title|body" combined. */

/**
 * Send a notification. Creates the FIFO if it doesn't exist yet.
 * Non-fatal if nothing is listening (e.g. BotDesk isn't running):
 * returns -1, but never blocks the caller for more than a brief
 * moment — the FIFO is opened O_NONBLOCK, so a missing reader is
 * reported immediately rather than hanging the caller.
 *
 * @param title  Short heading, shown in bold in the toast.
 * @param body   Message body. May be NULL/empty for a title-only toast.
 * @return       0 if the notification was handed off to the FIFO,
 *               -1 if nobody is listening or on error (errno set).
 */
int bot_notify_send(const char *title, const char *body);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_NOTIFY_H */
