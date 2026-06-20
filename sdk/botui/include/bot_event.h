/* ============================================================
 * BotOS Core — Event Loop API
 * ============================================================
 * File:    bot_event.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_EVENT_H
#define BOTOS_EVENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Event Types ─────────────────────────────────────────── */

typedef enum {
    BOT_EVENT_NONE       = 0,
    BOT_EVENT_KEY_DOWN   = 1,
    BOT_EVENT_KEY_UP     = 2,
    BOT_EVENT_MOUSE_MOVE = 3,
    BOT_EVENT_MOUSE_DOWN = 4,
    BOT_EVENT_MOUSE_UP   = 5,
    BOT_EVENT_SCROLL     = 6,
    BOT_EVENT_RESIZE     = 7,
    BOT_EVENT_CLOSE      = 8,
} bot_event_type_t;

/* ── Key Codes ───────────────────────────────────────────── */

typedef enum {
    BOT_KEY_UNKNOWN = 0,
    BOT_KEY_ESCAPE  = 27,
    BOT_KEY_ENTER   = 13,
    BOT_KEY_TAB     = 9,
    BOT_KEY_SPACE   = 32,
    BOT_KEY_BACKSPACE = 8,
    BOT_KEY_UP      = 256,
    BOT_KEY_DOWN    = 257,
    BOT_KEY_LEFT    = 258,
    BOT_KEY_RIGHT   = 259,
    /* Letters are their ASCII values: 'a'-'z', 'A'-'Z' */
} bot_key_t;

/* ── Mouse Buttons ───────────────────────────────────────── */

typedef enum {
    BOT_MOUSE_LEFT   = 1,
    BOT_MOUSE_MIDDLE = 2,
    BOT_MOUSE_RIGHT  = 3,
} bot_mouse_button_t;

/* ── Event Structure ─────────────────────────────────────── */

typedef struct bot_event {
    bot_event_type_t type;

    union {
        struct { int keycode; int modifiers; }       key;
        struct { int x; int y; int button; }         mouse;
        struct { int dx; int dy; }                    scroll;
        struct { int width; int height; }             resize;
    };
} bot_event_t;

/* ── Event Callback ──────────────────────────────────────── */

typedef void (*bot_event_callback_t)(const bot_event_t *event, void *data);

/* ── Event Loop API ──────────────────────────────────────── */

/** Poll for the next event (non-blocking). Returns 1 if event available. */
int bot_event_poll(bot_event_t *event);

/** Wait for the next event (blocking). */
void bot_event_wait(bot_event_t *event);

/** Register an event callback. */
void bot_event_on(bot_event_type_t type, bot_event_callback_t cb, void *data);

/** Run the event loop until close is requested. */
void bot_event_loop(void);

/** Request event loop exit. */
void bot_event_quit(void);

/** Push a synthetic event to the queue. */
int bot_event_push(const bot_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_EVENT_H */
