/* ============================================================
 * BotOS Core — Window Management API
 * ============================================================
 * File:    bot_window.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_WINDOW_H
#define BOTOS_WINDOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque window handle. */
typedef struct bot_window bot_window_t;

/**
 * Create a new window.
 * @param title   Window title.
 * @param width   Width in pixels.
 * @param height  Height in pixels.
 * @return        Window handle, or NULL on error.
 */
bot_window_t *bot_window_create(const char *title, int width, int height);

/** Show a window. */
void bot_window_show(bot_window_t *win);

/** Hide a window. */
void bot_window_hide(bot_window_t *win);

/** Set window title. */
void bot_window_set_title(bot_window_t *win, const char *title);

/** Resize a window. */
void bot_window_resize(bot_window_t *win, int width, int height);

/** Get the pixel framebuffer for direct drawing. */
uint32_t *bot_window_get_framebuffer(bot_window_t *win);

/** Flush the framebuffer to screen. */
void bot_window_flip(bot_window_t *win);

/** Destroy a window and free resources. */
void bot_window_destroy(bot_window_t *win);

/** Check if window close was requested. */
int bot_window_should_close(const bot_window_t *win);

/** Get the screen size of the display backend. */
void bot_window_get_screen_size(int *w, int *h);

/** Get active window size. */
void bot_window_get_active_size(int *w, int *h);

/** Get title bar height. */
int bot_window_get_title_h(void);

/** Set should_close flag on active window. */
void bot_window_set_should_close(int close);

/** Toggle active window maximize state. */
void bot_window_toggle_maximize(void);

/** Flip the active window. */
void bot_window_flip_active(void);

/**
 * Get the backend-native window handle.
 *
 * On the X11 backend this is the Window XID, cast to unsigned long.
 * On the framebuffer backend (no native windowing system exists to
 * hand out a handle for) this always returns 0.
 *
 * Intended for window-manager code (e.g. BotDesk) that needs to
 * reparent or otherwise directly manipulate windows via Xlib — most
 * callers should never need this.
 */
unsigned long bot_window_get_native_handle(bot_window_t *win);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_WINDOW_H */
