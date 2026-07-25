/* ============================================================
 * BotOS Core — botnotify CLI
 * ============================================================
 * File:    botnotify_cli.c
 * License: MIT
 *
 * Usage: botnotify "Title" ["Body text"]
 *
 * A thin command-line wrapper around bot_notify_send(), the same way
 * `notify-send` works on a conventional Linux desktop — lets shell
 * scripts, BotShell one-liners, or any other program that isn't
 * linked against botui pop up a toast without needing to know
 * anything about the FIFO transport underneath.
 * ============================================================ */

#include "bot_notify.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <title> [body]\n", argv[0]);
        return 2;
    }

    const char *title = argv[1];
    const char *body  = (argc >= 3) ? argv[2] : "";

    if (bot_notify_send(title, body) != 0) {
        fprintf(stderr, "botnotify: no listener (is BotDesk running?)\n");
        return 1;
    }
    return 0;
}
