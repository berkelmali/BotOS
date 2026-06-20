/* ============================================================
 * BotOS Core — Event Loop & Dispatcher (Production)
 * ============================================================
 * File:    event.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — Production event loop.
 *
 * Architecture:
 *   - Circular event queue (lock-free single producer)
 *   - Type-based callback dispatch table
 *   - X11 event translation (KeySym → bot_key_t, XButton → mouse)
 *   - select()-based wait (no busy spin)
 *   - Wildcard callbacks (BOT_EVENT_NONE = all events)
 *   - Callback unregistration
 *
 * Event flow:
 *   X11/platform → event_push() → queue → bot_event_poll()
 *                                            ↓
 *                                    callback dispatch
 * ============================================================ */

#include "bot_event.h"
#include "bot_window.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifndef BOTOS_HAS_X11
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/select.h>
#include <sys/ioctl.h>

static int g_ev_fds[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int g_ev_count = 0;
static int g_mouse_x = 512;
static int g_mouse_y = 384;

void bot_event_get_mouse_pos(int *x, int *y)
{
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
}

void bot_event_init_devs(void)
{
    g_ev_count = 0;
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            g_ev_fds[g_ev_count++] = fd;
        }
    }
}

void bot_event_shutdown_devs(void)
{
    for (int i = 0; i < g_ev_count; i++) {
        if (g_ev_fds[i] >= 0) {
            close(g_ev_fds[i]);
            g_ev_fds[i] = -1;
        }
    }
    g_ev_count = 0;
}
#else
void bot_event_get_mouse_pos(int *x, int *y)
{
    if (x) *x = 0;
    if (y) *y = 0;
}
void bot_event_init_devs(void) {}
void bot_event_shutdown_devs(void) {}
#endif

/* ── Platform Detection ──────────────────────────────────── */

#ifdef BOTOS_HAS_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/select.h>

/* Forward declaration — the display is owned by window.c */
extern Display *g_display;
extern Atom     g_wm_delete_message;
#endif

/* ── Callback Registry ───────────────────────────────────── */

#define MAX_CALLBACKS  64

typedef struct {
    int                  active;
    bot_event_type_t     type;       /**< BOT_EVENT_NONE = wildcard (all). */
    bot_event_callback_t callback;
    void                *data;
} callback_entry_t;

static callback_entry_t g_callbacks[MAX_CALLBACKS];
static int              g_callback_count = 0;

/* ── Event Queue (Lock-Free Circular Buffer) ─────────────── */

#define EVENT_QUEUE_SIZE  128  /* Must be power of 2 */
#define EVENT_QUEUE_MASK  (EVENT_QUEUE_SIZE - 1)

static bot_event_t g_event_queue[EVENT_QUEUE_SIZE];
static volatile int g_queue_head  = 0;  /* Consumer reads from here  */
static volatile int g_queue_tail  = 0;  /* Producer writes here      */
static volatile int g_queue_count = 0;

static volatile int g_quit_requested = 0;

/* ── Internal: Queue Operations ──────────────────────────── */

int bot_event_push(const bot_event_t *event)
{
    if (g_queue_count >= EVENT_QUEUE_SIZE) {
        /* Queue full — drop oldest event (overwrite) */
        g_queue_head = (g_queue_head + 1) & EVENT_QUEUE_MASK;
        g_queue_count--;
    }

    g_event_queue[g_queue_tail] = *event;
    g_queue_tail = (g_queue_tail + 1) & EVENT_QUEUE_MASK;
    g_queue_count++;

    return 0;
}

static int event_pop(bot_event_t *event)
{
    if (g_queue_count <= 0) return 0;

    if (event) {
        *event = g_event_queue[g_queue_head];
    }
    g_queue_head = (g_queue_head + 1) & EVENT_QUEUE_MASK;
    g_queue_count--;

    return 1;
}

/* ── Internal: Callback Dispatch ─────────────────────────── */

/**
 * Dispatch an event to all matching registered callbacks.
 * Callbacks registered with BOT_EVENT_NONE receive ALL events.
 */
static void dispatch_event(const bot_event_t *event)
{
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!g_callbacks[i].active) continue;

        if (g_callbacks[i].type == event->type ||
            g_callbacks[i].type == BOT_EVENT_NONE) {
            g_callbacks[i].callback(event, g_callbacks[i].data);
        }
    }
}

/* ── X11 Event Translation ───────────────────────────────── */

#ifdef BOTOS_HAS_X11

/**
 * Translate X11 KeySym to bot_key_t.
 */
static int x11_translate_key(KeySym ks)
{
    switch (ks) {
        case XK_Escape:    return BOT_KEY_ESCAPE;
        case XK_Return:    return BOT_KEY_ENTER;
        case XK_Tab:       return BOT_KEY_TAB;
        case XK_space:     return BOT_KEY_SPACE;
        case XK_BackSpace: return BOT_KEY_BACKSPACE;
        case XK_Up:        return BOT_KEY_UP;
        case XK_Down:      return BOT_KEY_DOWN;
        case XK_Left:      return BOT_KEY_LEFT;
        case XK_Right:     return BOT_KEY_RIGHT;
        default:
            /* ASCII letters and digits pass through */
            if (ks >= XK_a && ks <= XK_z) return (int)ks;
            if (ks >= XK_A && ks <= XK_Z) return (int)ks;
            if (ks >= XK_0 && ks <= XK_9) return (int)ks;
            return BOT_KEY_UNKNOWN;
    }
}

/**
 * Translate X11 modifier state to our modifier flags.
 */
static int x11_translate_mods(unsigned int state)
{
    int mods = 0;
    if (state & ShiftMask)   mods |= (1 << 0);  /* Shift */
    if (state & ControlMask) mods |= (1 << 1);  /* Ctrl  */
    if (state & Mod1Mask)    mods |= (1 << 2);  /* Alt   */
    return mods;
}

/**
 * Poll X11 events and push translated events to our queue.
 */
static void x11_pump_events(void)
{
    if (!g_display) return;

    while (XPending(g_display)) {
        XEvent xev;
        XNextEvent(g_display, &xev);

        bot_event_t ev;
        memset(&ev, 0, sizeof(ev));

        switch (xev.type) {
            case KeyPress: {
                KeySym ks = XLookupKeysym(&xev.xkey, 0);
                ev.type = BOT_EVENT_KEY_DOWN;
                ev.key.keycode   = x11_translate_key(ks);
                ev.key.modifiers = x11_translate_mods(xev.xkey.state);
                bot_event_push(&ev);
                break;
            }

            case KeyRelease: {
                KeySym ks = XLookupKeysym(&xev.xkey, 0);
                ev.type = BOT_EVENT_KEY_UP;
                ev.key.keycode   = x11_translate_key(ks);
                ev.key.modifiers = x11_translate_mods(xev.xkey.state);
                bot_event_push(&ev);
                break;
            }

            case ButtonPress: {
                if (xev.xbutton.button <= 3) {
                    ev.type = BOT_EVENT_MOUSE_DOWN;
                    ev.mouse.x      = xev.xbutton.x;
                    ev.mouse.y      = xev.xbutton.y;
                    ev.mouse.button = (int)xev.xbutton.button;
                    
                    /* Intercept title bar clicks for Left Mouse Button Down in X11 */
                    if (ev.mouse.button == BOT_MOUSE_LEFT) {
                        int ww = 0, wh = 0;
                        bot_window_get_active_size(&ww, &wh);
                        int title_h = bot_window_get_title_h();
                        if (ww > 0 && wh > 0 && title_h > 0) {
                            int rx = ev.mouse.x;
                            int ry = ev.mouse.y;
                            if (rx >= 0 && rx < ww && ry >= 0 && ry < title_h) {
                                if (rx >= ww - 20 && rx <= ww - 10) {
                                    bot_window_set_should_close(1);
                                    bot_event_t close_ev;
                                    memset(&close_ev, 0, sizeof(close_ev));
                                    close_ev.type = BOT_EVENT_CLOSE;
                                    bot_event_push(&close_ev);
                                    break;
                                } else if (rx >= ww - 38 && rx <= ww - 28) {
                                    bot_window_toggle_maximize();
                                    break;
                                } else if (rx >= ww - 56 && rx <= ww - 46) {
                                    bot_window_set_should_close(1);
                                    bot_event_t close_ev;
                                    memset(&close_ev, 0, sizeof(close_ev));
                                    close_ev.type = BOT_EVENT_CLOSE;
                                    bot_event_push(&close_ev);
                                    break;
                                }
                            }
                        }
                    }
                    bot_event_push(&ev);
                } else if (xev.xbutton.button == 4) {
                    /* Scroll up */
                    ev.type = BOT_EVENT_SCROLL;
                    ev.scroll.dx = 0;
                    ev.scroll.dy = -1;
                    bot_event_push(&ev);
                } else if (xev.xbutton.button == 5) {
                    /* Scroll down */
                    ev.type = BOT_EVENT_SCROLL;
                    ev.scroll.dx = 0;
                    ev.scroll.dy = 1;
                    bot_event_push(&ev);
                }
                break;
            }

            case ButtonRelease: {
                if (xev.xbutton.button <= 3) {
                    ev.type = BOT_EVENT_MOUSE_UP;
                    ev.mouse.x      = xev.xbutton.x;
                    ev.mouse.y      = xev.xbutton.y;
                    ev.mouse.button = (int)xev.xbutton.button;
                    bot_event_push(&ev);
                }
                break;
            }

            case MotionNotify: {
                ev.type = BOT_EVENT_MOUSE_MOVE;
                ev.mouse.x      = xev.xmotion.x;
                ev.mouse.y      = xev.xmotion.y;
                ev.mouse.button = 0;
                bot_event_push(&ev);
                break;
            }

            case ConfigureNotify: {
                ev.type = BOT_EVENT_RESIZE;
                ev.resize.width  = xev.xconfigure.width;
                ev.resize.height = xev.xconfigure.height;
                bot_event_push(&ev);
                break;
            }

            case ClientMessage: {
                if ((Atom)xev.xclient.data.l[0] == g_wm_delete_message) {
                    ev.type = BOT_EVENT_CLOSE;
                    bot_event_push(&ev);
                }
                break;
            }

            case Expose: {
                /* Window needs redraw — we handle this in flip() */
                break;
            }

            default:
                break;
        }
    }
}

/**
 * Block until X11 has events available, using select().
 */
static void x11_wait_events(void)
{
    if (!g_display) return;

    int x11_fd = ConnectionNumber(g_display);

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(x11_fd, &rfds);

    /* Wait up to 16ms (60fps frame budget) to remain responsive */
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 16000;

    select(x11_fd + 1, &rfds, NULL, NULL, &tv);
}

#endif /* BOTOS_HAS_X11 */

#ifndef BOTOS_HAS_X11
extern void bot_window_get_screen_size(int *w, int *h);

static void translate_mouse_coords(bot_event_t *ev)
{
    if (ev->type == BOT_EVENT_MOUSE_MOVE ||
        ev->type == BOT_EVENT_MOUSE_DOWN ||
        ev->type == BOT_EVENT_MOUSE_UP) {
        int sw = 1024, sh = 768;
        bot_window_get_screen_size(&sw, &sh);
        int ww = 0, wh = 0;
        bot_window_get_active_size(&ww, &wh);
        if (ww > 0 && wh > 0) {
            int dx = (sw - ww) / 2;
            int dy = (sh - wh) / 2;
            if (dx < 0) dx = 0;
            if (dy < 0) dy = 0;
            int app_x = ev->mouse.x - dx;
            int app_y = ev->mouse.y - dy;
            if (app_x < 0) app_x = 0;
            if (app_x >= ww) app_x = ww - 1;
            if (app_y < 0) app_y = 0;
            if (app_y >= wh) app_y = wh - 1;
            ev->mouse.x = app_x;
            ev->mouse.y = app_y;
        }
    }
}

static void push_evdev_mouse_event(bot_event_t *ev)
{
    /* Intercept title bar clicks for Left Mouse Button Down */
    if (ev->type == BOT_EVENT_MOUSE_DOWN && ev->mouse.button == BOT_MOUSE_LEFT) {
        int sw = 1024, sh = 768;
        bot_window_get_screen_size(&sw, &sh);
        int ww = 0, wh = 0;
        bot_window_get_active_size(&ww, &wh);
        int title_h = bot_window_get_title_h();
        if (ww > 0 && wh > 0 && title_h > 0) {
            int dx = (sw - ww) / 2;
            int dy = (sh - wh) / 2;
            if (dx < 0) dx = 0;
            if (dy < 0) dy = 0;
            int rx = ev->mouse.x - dx;
            int ry = ev->mouse.y - dy;
            if (rx >= 0 && rx < ww && ry >= 0 && ry < title_h) {
                if (rx >= ww - 20 && rx <= ww - 10) {
                    bot_window_set_should_close(1);
                    bot_event_t close_ev;
                    memset(&close_ev, 0, sizeof(close_ev));
                    close_ev.type = BOT_EVENT_CLOSE;
                    bot_event_push(&close_ev);
                    return;
                } else if (rx >= ww - 38 && rx <= ww - 28) {
                    bot_window_toggle_maximize();
                    return;
                } else if (rx >= ww - 56 && rx <= ww - 46) {
                    bot_window_set_should_close(1);
                    bot_event_t close_ev;
                    memset(&close_ev, 0, sizeof(close_ev));
                    close_ev.type = BOT_EVENT_CLOSE;
                    bot_event_push(&close_ev);
                    return;
                }
            }
        }
    }
    
    translate_mouse_coords(ev);
    bot_event_push(ev);
    bot_window_flip_active();
}

static int evdev_translate_key(int code)
{
    switch (code) {
        case 1:   return BOT_KEY_ESCAPE;
        case 28:  return BOT_KEY_ENTER;
        case 15:  return BOT_KEY_TAB;
        case 57:  return BOT_KEY_SPACE;
        case 14:  return BOT_KEY_BACKSPACE;
        case 103: return BOT_KEY_UP;
        case 108: return BOT_KEY_DOWN;
        case 105: return BOT_KEY_LEFT;
        case 106: return BOT_KEY_RIGHT;
        /* Letters mapping (standard QWERTY to ASCII) */
        case 30: return 'a'; case 48: return 'b'; case 46: return 'c';
        case 32: return 'd'; case 18: return 'e'; case 33: return 'f';
        case 34: return 'g'; case 35: return 'h'; case 23: return 'i';
        case 36: return 'j'; case 37: return 'k'; case 38: return 'l';
        case 50: return 'm'; case 49: return 'n'; case 24: return 'o';
        case 25: return 'p'; case 16: return 'q'; case 19: return 'r';
        case 31: return 's'; case 20: return 't'; case 22: return 'u';
        case 47: return 'v'; case 17: return 'w'; case 45: return 'x';
        case 21: return 'y'; case 44: return 'z';
        /* Digits mapping */
        case 11: return '0'; case 2: return '1'; case 3: return '2';
        case 4: return '3'; case 5: return '4'; case 6: return '5';
        case 7: return '6'; case 8: return '7'; case 9: return '8';
        case 10: return '9';
        default:  return BOT_KEY_UNKNOWN;
    }
}

static void evdev_pump_events(void)
{
    static int g_pending_move = 0;
    static int g_pending_btn_event = 0; /* 1 = down, 2 = up */
    static int g_pending_btn = 0;

    int sw = 1024, sh = 768;
    bot_window_get_screen_size(&sw, &sh);

    for (int i = 0; i < g_ev_count; i++) {
        int fd = g_ev_fds[i];
        if (fd < 0) continue;

        struct input_event evs[64];
        ssize_t bytes = read(fd, evs, sizeof(evs));
        if (bytes <= 0) continue;

        int num_events = (int)(bytes / sizeof(struct input_event));
        for (int j = 0; j < num_events; j++) {
            struct input_event *e = &evs[j];
            bot_event_t bot_ev;
            memset(&bot_ev, 0, sizeof(bot_ev));

            if (e->type == EV_REL) {
                if (e->code == REL_X) {
                    g_mouse_x += e->value;
                    if (g_mouse_x < 0) g_mouse_x = 0;
                    if (g_mouse_x >= sw) g_mouse_x = sw - 1;
                    g_pending_move = 1;
                } else if (e->code == REL_Y) {
                    g_mouse_y += e->value;
                    if (g_mouse_y < 0) g_mouse_y = 0;
                    if (g_mouse_y >= sh) g_mouse_y = sh - 1;
                    g_pending_move = 1;
                } else if (e->code == REL_WHEEL) {
                    bot_ev.type = BOT_EVENT_SCROLL;
                    bot_ev.scroll.dx = 0;
                    bot_ev.scroll.dy = e->value;
                    bot_event_push(&bot_ev);
                }
            } else if (e->type == EV_ABS) {
                struct input_absinfo abs_info;
                if (e->code == ABS_X) {
                    if (ioctl(fd, EVIOCGABS(ABS_X), &abs_info) >= 0 && abs_info.maximum > abs_info.minimum) {
                        g_mouse_x = (e->value - abs_info.minimum) * sw / (abs_info.maximum - abs_info.minimum);
                        if (g_mouse_x < 0) g_mouse_x = 0;
                        if (g_mouse_x >= sw) g_mouse_x = sw - 1;
                        g_pending_move = 1;
                    }
                } else if (e->code == ABS_Y) {
                    if (ioctl(fd, EVIOCGABS(ABS_Y), &abs_info) >= 0 && abs_info.maximum > abs_info.minimum) {
                        g_mouse_y = (e->value - abs_info.minimum) * sh / (abs_info.maximum - abs_info.minimum);
                        if (g_mouse_y < 0) g_mouse_y = 0;
                        if (g_mouse_y >= sh) g_mouse_y = sh - 1;
                        g_pending_move = 1;
                    }
                }
            } else if (e->type == EV_KEY) {
                if (e->code >= 0x110 && e->code <= 0x112) {
                    /* Mouse button */
                    int btn = BOT_MOUSE_LEFT;
                    if (e->code == 273) btn = BOT_MOUSE_RIGHT;
                    else if (e->code == 274) btn = BOT_MOUSE_MIDDLE;

                    g_pending_btn_event = (e->value != 0) ? 1 : 2;
                    g_pending_btn = btn;
                } else {
                    /* Keyboard key */
                    int keycode = evdev_translate_key(e->code);
                    if (keycode != BOT_KEY_UNKNOWN) {
                        bot_ev.type = (e->value != 0) ? BOT_EVENT_KEY_DOWN : BOT_EVENT_KEY_UP;
                        bot_ev.key.keycode = keycode;
                        bot_ev.key.modifiers = 0;
                        bot_event_push(&bot_ev);
                    }
                }
            } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
                if (g_pending_btn_event) {
                    bot_ev.type = (g_pending_btn_event == 1) ? BOT_EVENT_MOUSE_DOWN : BOT_EVENT_MOUSE_UP;
                    bot_ev.mouse.x = g_mouse_x;
                    bot_ev.mouse.y = g_mouse_y;
                    bot_ev.mouse.button = g_pending_btn;
                    push_evdev_mouse_event(&bot_ev);
                } else if (g_pending_move) {
                    bot_ev.type = BOT_EVENT_MOUSE_MOVE;
                    bot_ev.mouse.x = g_mouse_x;
                    bot_ev.mouse.y = g_mouse_y;
                    push_evdev_mouse_event(&bot_ev);
                }
                g_pending_btn_event = 0;
                g_pending_btn = 0;
                g_pending_move = 0;
            }
        }
    }
}
#endif

/* ── Public API ──────────────────────────────────────────── */

int bot_event_poll(bot_event_t *event)
{
    /* Pump platform events into our queue */
#ifdef BOTOS_HAS_X11
    x11_pump_events();
#else
    evdev_pump_events();
#endif

    /* Pop from internal queue */
    if (!event_pop(event)) {
        if (event) event->type = BOT_EVENT_NONE;
        return 0;
    }

    /* Dispatch to registered callbacks */
    if (event) {
        dispatch_event(event);
    }

    return 1;
}

void bot_event_wait(bot_event_t *event)
{
    /* Try polling first */
    if (bot_event_poll(event)) return;

    /* Block until events arrive */
#ifdef BOTOS_HAS_X11
    x11_wait_events();
#else
    /* Framebuffer mode: sleep briefly to avoid 100% CPU */
    {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 16000000 };  /* 16ms */
        nanosleep(&ts, NULL);
    }
#endif

    /* Try again after waking */
    bot_event_poll(event);
}

void bot_event_on(bot_event_type_t type, bot_event_callback_t cb, void *data)
{
    if (!cb) return;

    /* Find free slot */
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!g_callbacks[i].active) {
            g_callbacks[i].active   = 1;
            g_callbacks[i].type     = type;
            g_callbacks[i].callback = cb;
            g_callbacks[i].data     = data;
            g_callback_count++;
            return;
        }
    }

    fprintf(stderr, "[botui] Event callback table full (max %d)\n", MAX_CALLBACKS);
}

void bot_event_loop(void)
{
    bot_event_t event;
    g_quit_requested = 0;

    while (!g_quit_requested) {
        bot_event_wait(&event);

        if (event.type == BOT_EVENT_CLOSE) {
            g_quit_requested = 1;
        }
    }
}

void bot_event_quit(void)
{
    g_quit_requested = 1;

    /* Push a synthetic close event to break out of wait */
    bot_event_t close_event;
    memset(&close_event, 0, sizeof(close_event));
    close_event.type = BOT_EVENT_CLOSE;
    bot_event_push(&close_event);
}
