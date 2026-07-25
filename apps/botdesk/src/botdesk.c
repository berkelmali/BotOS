/* ============================================================
 * BotOS Core — BotDesk Desktop Shell (Production)
 * ============================================================
 * File:    botdesk.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — Production desktop environment.
 * Refactored to use the bot_widget toolkit.
 *
 * Layout (1024×768):
 *   ┌─────────────────────────────────────────┐
 *   │              Desktop Area               │
 *   │          (gradient + icons)             │
 *   ├─────────┬───────────────────────┬───────┤ y=736
 *   │ ◉ Menu  │     Window List      │ Clock │ Taskbar
 *   └─────────┴───────────────────────┴───────┘ y=768
 *
 * 2026-07 update — real window management:
 *
 *   When built with X11 (BOTOS_HAS_X11), BotDesk now acts as a
 *   minimal reparenting window manager (the same basic technique
 *   dwm/tinywm/9wm use) instead of a single-app fullscreen
 *   launcher: it claims SubstructureRedirect on the root window,
 *   wraps every app window it's asked to map in a decorated frame
 *   (title bar + close button, draggable), and keeps its own
 *   desktop/taskbar window alive underneath so multiple apps can
 *   run — and be seen — at once. The taskbar's window-list area
 *   lists real open windows; clicking one raises and focuses it.
 *
 *   If X11 isn't available, or another window manager already
 *   owns the display (SubstructureRedirect fails), BotDesk falls
 *   back to the original behavior: launch one app at a time,
 *   fullscreen, blocking — there's no way to show two windows at
 *   once on a bare framebuffer, so that fallback is intentional,
 *   not a bug.
 * ============================================================ */

#include "bot_ui.h"
#include "bot_log.h"
#include "bot_notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* ── Layout Constants ────────────────────────────────────── */

#define DESK_WIDTH       1024
#define DESK_HEIGHT      768
#define TASKBAR_HEIGHT   32
#define TASKBAR_Y        (DESK_HEIGHT - TASKBAR_HEIGHT)
#define MENU_BTN_WIDTH   80
#define MENU_BTN_HEIGHT  (TASKBAR_HEIGHT - 4)
#define CLOCK_WIDTH      80
#define ICON_SIZE        48
#define ICON_GRID_X      40
#define ICON_GRID_Y      40
#define ICON_SPACING     80

/* ── Desktop State Forward Declarations ──────────────────── */
static bot_window_t *g_window      = NULL;
static bot_canvas_t *g_canvas      = NULL;
static int           g_needs_redraw = 1;

/* ── Wallpaper Colors ────────────────────────────────────── */

#define COL_BG_TOP       BOT_RGBA(12,  15,  30,  255)
#define COL_BG_BOT       BOT_RGBA(25,  35,  65,  255)

/* ════════════════════════════════════════════════════════════
 *  WINDOW MANAGER (X11 only)
 *
 *  Everything in this section is BotDesk acting as a window
 *  manager for *other* processes' windows, as opposed to the
 *  rest of the file, which is BotDesk drawing its own single
 *  desktop/taskbar window via bot_canvas like any other app.
 *  The two are deliberately kept on separate X connections
 *  (wm_dpy here vs. the one bot_ui/window.c owns internally) so
 *  this section never has to reach into botui's internals.
 * ════════════════════════════════════════════════════════════ */
/* ── Desktop Icon ────────────────────────────────────────── */

typedef struct {
    const char *label;
    int         x, y;
    bot_color_t icon_color;
} desktop_icon_t;

static desktop_icon_t g_icons[] = {
    { "Shell",   0, 0, BOT_RGBA(80,  200, 120, 255) },
    { "Paint",   0, 0, BOT_RGBA(255, 120, 80,  255) },
    { "Editor",  0, 0, BOT_RGBA(80,  160, 255, 255) },
    { "Files",   0, 0, BOT_RGBA(255, 200, 60,  255) },
    { "BotPkg",  0, 0, BOT_RGBA(180, 100, 255, 255) },
    { "Settings",0, 0, BOT_RGBA(120, 220, 220, 255) },
    { "Info",    0, 0, BOT_RGBA(255, 105, 180, 255) },
};

#define ICON_COUNT  (int)(sizeof(g_icons) / sizeof(g_icons[0]))


#ifdef BOTOS_HAS_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#define WM_MAX_WINDOWS   32
#define WM_TITLE_H       26
#define WM_CLOSE_SIZE    18
#define WM_TASKBAR_BTN_W 150
#define WM_BORDER        5    /* grabbable margin on left/right/bottom for resize */
#define WM_GRIP_SIZE     14   /* resize-corner hit zone in the bottom-right */
#define WM_MIN_W         200
#define WM_MIN_H         150

typedef struct {
    int    used;
    Window frame;
    Window client;
    char   title[128];
    int    x, y, w, h;   /* frame geometry (includes title bar) */
    int    focused;
} wm_managed_t;

static Display    *wm_dpy          = NULL;
static Window       wm_root;
static int           wm_active      = 0;
static wm_managed_t  wm_windows[WM_MAX_WINDOWS];
static int           wm_window_count = 0;
static Atom           wm_atom_delete;
static Atom           wm_atom_protocols;
static GC              wm_gc;
static XFontStruct     *wm_font = NULL;

static int    wm_dragging    = 0;
static Window wm_drag_frame  = 0;
static int    wm_drag_off_x  = 0;
static int    wm_drag_off_y  = 0;

static int    wm_resizing      = 0;
static Window wm_resize_frame  = 0;
static int    wm_resize_start_root_x = 0;
static int    wm_resize_start_root_y = 0;
static int    wm_resize_start_w      = 0;
static int    wm_resize_start_h      = 0;

static volatile sig_atomic_t wm_xerror_occurred = 0;

static int wm_temp_error_handler(Display *d, XErrorEvent *e)
{
    (void)d; (void)e;
    wm_xerror_occurred = 1;
    return 0;
}

/* Once running, BadWindow is routine (a client can vanish between an
 * event being queued and us handling it) — every reparenting WM has
 * to tolerate that race rather than treat it as fatal. Anything else
 * gets logged so real problems aren't silently swallowed. */
static int wm_runtime_error_handler(Display *d, XErrorEvent *e)
{
    if (e->error_code == BadWindow) return 0;
    char msg[128];
    XGetErrorText(d, e->error_code, msg, sizeof(msg));
    BOT_LOG_WARN("X11 error in WM: %s (request %d)", msg, e->request_code);
    return 0;
}

static wm_managed_t *wm_find_by_frame(Window w)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (wm_windows[i].used && wm_windows[i].frame == w) return &wm_windows[i];
    return NULL;
}

static wm_managed_t *wm_find_by_client(Window w)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (wm_windows[i].used && wm_windows[i].client == w) return &wm_windows[i];
    return NULL;
}

static void wm_draw_decoration(wm_managed_t *mw)
{
    if (!mw || !wm_dpy) return;

    bot_theme_t *th = bot_theme_get();
    unsigned long bar_bg    = (mw->focused ? th->accent : th->panel) & 0x00FFFFFFUL;
    unsigned long border_bg = (mw->focused ? th->accent : th->panel_border) & 0x00FFFFFFUL;
    unsigned long bar_fg    = th->fg & 0x00FFFFFFUL;

    XSetForeground(wm_dpy, wm_gc, bar_bg);
    XFillRectangle(wm_dpy, mw->frame, wm_gc, 0, 0, (unsigned)mw->w, WM_TITLE_H);

    /* Side/bottom margin: the client sits inset by WM_BORDER on the
     * left/right/bottom (not just below the title bar), so there's an
     * actual grabbable strip of frame-owned pixels to resize from —
     * clicking a 1px window edge is impractical, and without this
     * margin there would be nothing there to click at all. */
    XSetForeground(wm_dpy, wm_gc, border_bg);
    XFillRectangle(wm_dpy, mw->frame, wm_gc, 0, WM_TITLE_H, (unsigned)WM_BORDER, (unsigned)(mw->h - WM_TITLE_H));
    XFillRectangle(wm_dpy, mw->frame, wm_gc, mw->w - WM_BORDER, WM_TITLE_H, (unsigned)WM_BORDER, (unsigned)(mw->h - WM_TITLE_H));
    XFillRectangle(wm_dpy, mw->frame, wm_gc, 0, mw->h - WM_BORDER, (unsigned)mw->w, (unsigned)WM_BORDER);

    /* Resize grip: a few diagonal ticks in the bottom-right corner,
     * the classic affordance for "drag here to resize". */
    XSetForeground(wm_dpy, wm_gc, bar_fg);
    for (int i = 1; i <= 3; i++) {
        int off = i * 4;
        XDrawLine(wm_dpy, mw->frame, wm_gc,
                  mw->w - off, mw->h - 2,
                  mw->w - 2, mw->h - off);
    }

    int cx = mw->w - WM_CLOSE_SIZE - 5;
    int cy = (WM_TITLE_H - WM_CLOSE_SIZE) / 2;
    XSetForeground(wm_dpy, wm_gc, 0x00B33A3AUL);
    XFillRectangle(wm_dpy, mw->frame, wm_gc, cx, cy, WM_CLOSE_SIZE, WM_CLOSE_SIZE);
    XSetForeground(wm_dpy, wm_gc, 0x00F0F0F0UL);
    XDrawLine(wm_dpy, mw->frame, wm_gc, cx + 4, cy + 4, cx + WM_CLOSE_SIZE - 4, cy + WM_CLOSE_SIZE - 4);
    XDrawLine(wm_dpy, mw->frame, wm_gc, cx + WM_CLOSE_SIZE - 4, cy + 4, cx + 4, cy + WM_CLOSE_SIZE - 4);

    if (wm_font) {
        XSetForeground(wm_dpy, wm_gc, bar_fg);
        XSetFont(wm_dpy, wm_gc, wm_font->fid);
        XDrawString(wm_dpy, mw->frame, wm_gc, 8, WM_TITLE_H - 8,
                    mw->title, (int)strlen(mw->title));
    }
}

static void wm_set_focus(wm_managed_t *mw)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].focused && &wm_windows[i] != mw) {
            wm_windows[i].focused = 0;
            wm_draw_decoration(&wm_windows[i]);
        }
    }
    if (!mw) return;
    mw->focused = 1;
    XRaiseWindow(wm_dpy, mw->frame);
    XSetInputFocus(wm_dpy, mw->client, RevertToPointerRoot, CurrentTime);
    wm_draw_decoration(mw);
    g_needs_redraw = 1;
}

static void wm_remove_managed(wm_managed_t *mw)
{
    if (!mw) return;
    int was_focused = mw->focused;
    XDestroyWindow(wm_dpy, mw->frame);
    mw->used = 0;
    if (wm_window_count > 0) wm_window_count--;

    if (was_focused) {
        /* Hand focus to whatever else is open, if anything. */
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (wm_windows[i].used) { wm_set_focus(&wm_windows[i]); break; }
        }
    }
    g_needs_redraw = 1;
}

static void wm_handle_map_request(XMapRequestEvent *ev)
{
    Window client = ev->window;

    /* Never manage our own desktop/taskbar window. */
    if (g_window && client == (Window)bot_window_get_native_handle(g_window)) {
        XMapWindow(wm_dpy, client);
        return;
    }

    if (wm_find_by_client(client)) {
        /* Already managed (e.g. a duplicate MapRequest) — just show it. */
        XMapWindow(wm_dpy, client);
        return;
    }

    if (wm_window_count >= WM_MAX_WINDOWS) {
        XMapWindow(wm_dpy, client); /* out of slots — show unmanaged rather than hide it */
        return;
    }

    XWindowAttributes attrs;
    if (!XGetWindowAttributes(wm_dpy, client, &attrs)) return;
    int cw = attrs.width  > 0 ? attrs.width  : 640;
    int ch = attrs.height > 0 ? attrs.height : 480;

    int fw = cw + WM_BORDER * 2;
    int fh = ch + WM_TITLE_H + WM_BORDER;
    int fx = 48 + (wm_window_count % 6) * 28;
    int fy = 48 + (wm_window_count % 6) * 28;

    Window frame = XCreateSimpleWindow(wm_dpy, wm_root, fx, fy,
                                        (unsigned)fw, (unsigned)fh, 1,
                                        0x00505A78UL, 0x001A213AUL);
    XSelectInput(wm_dpy, frame,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | SubstructureNotifyMask);

    XSelectInput(wm_dpy, client, StructureNotifyMask | PropertyChangeMask);
    XSetWMProtocols(wm_dpy, client, &wm_atom_delete, 1);
    XReparentWindow(wm_dpy, client, frame, WM_BORDER, WM_TITLE_H);
    XMapWindow(wm_dpy, client);
    XMapWindow(wm_dpy, frame);

    int idx = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (!wm_windows[i].used) { idx = i; break; }
    if (idx < 0) { XDestroyWindow(wm_dpy, frame); return; } /* shouldn't happen, count checked above */

    wm_managed_t *mw = &wm_windows[idx];
    memset(mw, 0, sizeof(*mw));
    mw->used   = 1;
    mw->frame  = frame;
    mw->client = client;
    mw->x = fx; mw->y = fy; mw->w = fw; mw->h = fh;

    char *name = NULL;
    if (XFetchName(wm_dpy, client, &name) && name && name[0]) {
        strncpy(mw->title, name, sizeof(mw->title) - 1);
    } else {
        strncpy(mw->title, "BotOS App", sizeof(mw->title) - 1);
    }
    if (name) XFree(name);

    wm_window_count++;
    wm_set_focus(mw);
}

static void wm_handle_configure_request(XConfigureRequestEvent *ev)
{
    XWindowChanges changes;
    changes.x            = ev->x;
    changes.y            = ev->y;
    changes.width        = ev->width;
    changes.height       = ev->height;
    changes.border_width = ev->border_width;
    changes.sibling      = ev->above;
    changes.stack_mode   = ev->detail;
    XConfigureWindow(wm_dpy, ev->window, (unsigned)ev->value_mask, &changes);

    wm_managed_t *mw = wm_find_by_client(ev->window);
    if (mw && (ev->value_mask & (CWWidth | CWHeight))) {
        if (ev->value_mask & CWWidth)  mw->w = ev->width  + WM_BORDER * 2;
        if (ev->value_mask & CWHeight) mw->h = ev->height + WM_TITLE_H + WM_BORDER;
        XResizeWindow(wm_dpy, mw->frame, (unsigned)mw->w, (unsigned)mw->h);
        wm_draw_decoration(mw);
    }
}

static void wm_handle_destroy_notify(XDestroyWindowEvent *ev)
{
    wm_managed_t *mw = wm_find_by_client(ev->window);
    if (mw) wm_remove_managed(mw);
}

static void wm_handle_unmap_notify(XUnmapEvent *ev)
{
    wm_managed_t *mw = wm_find_by_client(ev->window);
    /* Only react when the unmap is reported relative to our frame
     * (the client withdrawing itself) — reparenting itself can emit
     * an UnmapNotify relative to root, which isn't a real close. */
    if (mw && ev->event == mw->frame) wm_remove_managed(mw);
}

static void wm_handle_expose(XExposeEvent *ev)
{
    wm_managed_t *mw = wm_find_by_frame(ev->window);
    if (mw) wm_draw_decoration(mw);
}

static void wm_handle_button_press(XButtonEvent *ev)
{
    wm_managed_t *mw = wm_find_by_frame(ev->window);
    if (!mw) return;

    wm_set_focus(mw);

    int cx = mw->w - WM_CLOSE_SIZE - 5;
    int cy = (WM_TITLE_H - WM_CLOSE_SIZE) / 2;
    if (ev->y >= cy && ev->y < cy + WM_CLOSE_SIZE &&
        ev->x >= cx && ev->x < cx + WM_CLOSE_SIZE) {
        XEvent msg;
        memset(&msg, 0, sizeof(msg));
        msg.xclient.type         = ClientMessage;
        msg.xclient.window       = mw->client;
        msg.xclient.message_type = wm_atom_protocols;
        msg.xclient.format       = 32;
        msg.xclient.data.l[0]    = (long)wm_atom_delete;
        msg.xclient.data.l[1]    = CurrentTime;
        XSendEvent(wm_dpy, mw->client, False, NoEventMask, &msg);
        return;
    }

    /* Bottom-right corner (the drawn resize grip) starts a resize
     * instead of a move. */
    if (ev->x >= mw->w - WM_GRIP_SIZE && ev->y >= mw->h - WM_GRIP_SIZE) {
        wm_resizing              = 1;
        wm_resize_frame          = mw->frame;
        wm_resize_start_root_x   = ev->x_root;
        wm_resize_start_root_y   = ev->y_root;
        wm_resize_start_w        = mw->w;
        wm_resize_start_h        = mw->h;
        return;
    }

    if (ev->y < WM_TITLE_H) {
        wm_dragging   = 1;
        wm_drag_frame = mw->frame;
        wm_drag_off_x = ev->x;
        wm_drag_off_y = ev->y;
    }
}

static void wm_handle_motion(XMotionEvent *ev)
{
    if (wm_resizing) {
        wm_managed_t *mw = wm_find_by_frame(wm_resize_frame);
        if (!mw) { wm_resizing = 0; return; }
        int dw = ev->x_root - wm_resize_start_root_x;
        int dh = ev->y_root - wm_resize_start_root_y;
        int nw = wm_resize_start_w + dw;
        int nh = wm_resize_start_h + dh;
        if (nw < WM_MIN_W) nw = WM_MIN_W;
        if (nh < WM_MIN_H) nh = WM_MIN_H;

        mw->w = nw;
        mw->h = nh;
        XResizeWindow(wm_dpy, mw->frame, (unsigned)nw, (unsigned)nh);
        XResizeWindow(wm_dpy, mw->client,
                      (unsigned)(nw - WM_BORDER * 2),
                      (unsigned)(nh - WM_TITLE_H - WM_BORDER));
        wm_draw_decoration(mw);
        return;
    }

    if (!wm_dragging) return;
    wm_managed_t *mw = wm_find_by_frame(wm_drag_frame);
    if (!mw) { wm_dragging = 0; return; }
    int nx = ev->x_root - wm_drag_off_x;
    int ny = ev->y_root - wm_drag_off_y;
    XMoveWindow(wm_dpy, mw->frame, nx, ny);
    mw->x = nx; mw->y = ny;
}

static void wm_handle_button_release(XButtonEvent *ev)
{
    (void)ev;
    wm_dragging = 0;
    wm_resizing = 0;
}

/* Cycles focus to the next (or, with Shift held, previous) managed
 * window — the same immediate-cycle behavior classic minimal window
 * managers use; no on-screen switcher overlay, just repeated Alt+Tab
 * taps walking the list. */
static void wm_cycle_focus(int forward)
{
    if (wm_window_count == 0) return;

    int cur = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].focused) { cur = i; break; }
    }

    int idx = cur;
    for (int step = 0; step < WM_MAX_WINDOWS; step++) {
        idx = forward ? (idx + 1) % WM_MAX_WINDOWS
                      : (idx - 1 + WM_MAX_WINDOWS) % WM_MAX_WINDOWS;
        if (idx == cur) break;
        if (wm_windows[idx].used) {
            wm_set_focus(&wm_windows[idx]);
            return;
        }
    }
    /* Only one window open (or none focused yet) — focus it. */
    if (cur < 0) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (wm_windows[i].used) { wm_set_focus(&wm_windows[i]); return; }
        }
    }
}

/* ════════════════════════════════════════════════════════════
 *  TOAST NOTIFICATIONS
 *
 *  Any process can pop up a toast via bot_notify_send() (see
 *  services/botnotify) without linking against botui or knowing
 *  anything about X11 — it just writes "title|body" to a well-known
 *  FIFO. BotDesk polls that FIFO here and renders whatever arrives
 *  as a small, non-focus-stealing window in the bottom-right corner
 *  that dismisses itself after a few seconds.
 *
 *  The toast window is override-redirect: it's a plain child of root
 *  like any client BotDesk manages, but override-redirect tells the
 *  X server to skip window-manager redirection entirely for it — a
 *  standard, correct way for a WM's own utility windows (tooltips,
 *  popups, notifications) to avoid being caught by its own
 *  SubstructureRedirect and treated as a new app to decorate.
 * ════════════════════════════════════════════════════════════ */

#define TOAST_W            300
#define TOAST_H             70
#define TOAST_MARGIN        16
#define TOAST_DURATION_MS 4000

static Window   toast_window   = 0;
static int      toast_active   = 0;
static char     toast_title[128] = "";
static char     toast_body[128]  = "";
static uint32_t toast_expire_at  = 0;
static int      notify_fifo_fd   = -1;

static void notify_fifo_open(void)
{
    mkfifo(BOT_NOTIFY_FIFO_PATH, 0666); /* harmless if it already exists */
    notify_fifo_fd = open(BOT_NOTIFY_FIFO_PATH, O_RDONLY | O_NONBLOCK);
}

static void toast_create_window_if_needed(void)
{
    if (toast_window) return;
    int sw = DisplayWidth(wm_dpy, DefaultScreen(wm_dpy));
    int sh = DisplayHeight(wm_dpy, DefaultScreen(wm_dpy));
    int x = sw - TOAST_W - TOAST_MARGIN;
    int y = sh - TOAST_H - TOAST_MARGIN - TASKBAR_HEIGHT;

    toast_window = XCreateSimpleWindow(wm_dpy, wm_root, x, y, TOAST_W, TOAST_H, 1,
                                        0x00505A78UL, 0x00182038UL);
    XSetWindowAttributes swa;
    swa.override_redirect = True;
    XChangeWindowAttributes(wm_dpy, toast_window, CWOverrideRedirect, &swa);
    XSelectInput(wm_dpy, toast_window, ExposureMask);
}

static void toast_draw(void)
{
    if (!toast_window) return;
    unsigned long bg = 0x00182038UL, accent = 0x00618FFFUL, fg = 0x00E8ECF5UL;

    XSetForeground(wm_dpy, wm_gc, bg);
    XFillRectangle(wm_dpy, toast_window, wm_gc, 0, 0, TOAST_W, TOAST_H);
    XSetForeground(wm_dpy, wm_gc, accent);
    XFillRectangle(wm_dpy, toast_window, wm_gc, 0, 0, 4, TOAST_H); /* accent stripe */

    if (wm_font) {
        XSetFont(wm_dpy, wm_gc, wm_font->fid);
        XSetForeground(wm_dpy, wm_gc, accent);
        XDrawString(wm_dpy, toast_window, wm_gc, 16, 26, toast_title, (int)strlen(toast_title));
        XSetForeground(wm_dpy, wm_gc, fg);
        XDrawString(wm_dpy, toast_window, wm_gc, 16, 46, toast_body, (int)strlen(toast_body));
    }
}

static void toast_show(const char *title, const char *body)
{
    toast_create_window_if_needed();
    strncpy(toast_title, title, sizeof(toast_title) - 1);
    toast_title[sizeof(toast_title) - 1] = '\0';
    strncpy(toast_body, body, sizeof(toast_body) - 1);
    toast_body[sizeof(toast_body) - 1] = '\0';

    XMapRaised(wm_dpy, toast_window);
    toast_draw();
    toast_active = 1;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    toast_expire_at = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000) + TOAST_DURATION_MS;
}

static void toast_check_expiry(void)
{
    if (!toast_active) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t now = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    if (now >= toast_expire_at) {
        XUnmapWindow(wm_dpy, toast_window);
        toast_active = 0;
    }
}

/* Non-blocking: reads at most one notification per call. A single
 * bot_notify_send() call always writes its whole "title|body" message
 * in one write(), and PIPE_BUF-sized single writes to a FIFO are
 * atomic on Linux, so one read() here always yields exactly one
 * complete, unsplit message.
 *
 * Note on FIFO semantics (verified empirically, not just assumed):
 * a non-blocking read() on a FIFO with no data AND no writer
 * currently connected returns 0 — this is NOT a one-time end-of-file
 * the way it would be for a regular file or a socket after the peer
 * closes. It's the FIFO's normal "nothing to read right now" state,
 * and the exact same long-lived reader fd correctly picks up a
 * message from a writer that connects later. An earlier version of
 * this function treated that 0 as EOF and closed+reopened the fd
 * every time it was called with nothing waiting, which raced against
 * botnotify: if a writer's open() landed in the brief window the
 * reader had just closed, it failed with ENXIO ("no listener") even
 * though BotDesk was running right next to it. */
static void notify_poll(void)
{
    if (notify_fifo_fd < 0) {
        notify_fifo_open();
        if (notify_fifo_fd < 0) return;
    }

    char buf[BOT_NOTIFY_MAX_LEN];
    ssize_t n = read(notify_fifo_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        char *sep = strchr(buf, '|');
        if (sep) {
            *sep = '\0';
            toast_show(buf, sep + 1);
        } else {
            toast_show(buf, "");
        }
    }
    /* n == 0 or n < 0 (EAGAIN): nothing waiting right now — leave the
     * fd open and check again on the next poll. */
}

/* ════════════════════════════════════════════════════════════
 *  COMMAND PALETTE (Ctrl+Space)
 *
 *  A keyboard-driven quick launcher, the same idea as Spotlight or a
 *  VS Code/Sublime command palette: tap Ctrl+Space, type a few
 *  letters of an app's name, hit Enter. Filters live against the same
 *  desktop icon list BotDesk already draws, so adding an app to one
 *  automatically adds it to the other.
 * ════════════════════════════════════════════════════════════ */

#define PALETTE_W       360
#define PALETTE_ROW_H    22
#define PALETTE_MAX_ROWS  6

static Window palette_window   = 0;
static int    palette_active   = 0;
static char   palette_query[64] = "";
static int    palette_selected  = 0;
static int    palette_matches[PALETTE_MAX_ROWS];
static int    palette_match_count = 0;

static void palette_recompute_matches(void)
{
    palette_match_count = 0;
    for (int i = 0; i < ICON_COUNT && palette_match_count < PALETTE_MAX_ROWS; i++) {
        if (palette_query[0] == '\0') {
            palette_matches[palette_match_count++] = i;
            continue;
        }
        /* Case-insensitive substring match against the same labels
         * the desktop icons use. */
        char hay[32], needle[64];
        size_t hlen = strlen(g_icons[i].label), j;
        for (j = 0; j < hlen && j < sizeof(hay) - 1; j++) hay[j] = (char)tolower((unsigned char)g_icons[i].label[j]);
        hay[j] = '\0';
        size_t qlen = strlen(palette_query);
        for (j = 0; j < qlen && j < sizeof(needle) - 1; j++) needle[j] = (char)tolower((unsigned char)palette_query[j]);
        needle[j] = '\0';
        if (strstr(hay, needle)) {
            palette_matches[palette_match_count++] = i;
        }
    }
    if (palette_selected >= palette_match_count) palette_selected = palette_match_count > 0 ? palette_match_count - 1 : 0;
}

static void palette_create_window_if_needed(void)
{
    if (palette_window) return;
    int sw = DisplayWidth(wm_dpy, DefaultScreen(wm_dpy));
    int x = (sw - PALETTE_W) / 2;
    int y = 120;
    int h = 34 + PALETTE_MAX_ROWS * PALETTE_ROW_H;

    palette_window = XCreateSimpleWindow(wm_dpy, wm_root, x, y, PALETTE_W, (unsigned)h, 1,
                                          0x00618FFFUL, 0x00141A2EUL);
    XSetWindowAttributes swa;
    swa.override_redirect = True;
    XChangeWindowAttributes(wm_dpy, palette_window, CWOverrideRedirect, &swa);
    XSelectInput(wm_dpy, palette_window, ExposureMask);
}

static void palette_draw(void)
{
    if (!palette_window) return;
    XSetForeground(wm_dpy, wm_gc, 0x00141A2EUL);
    XFillRectangle(wm_dpy, palette_window, wm_gc, 0, 0, PALETTE_W, 34 + PALETTE_MAX_ROWS * PALETTE_ROW_H);

    XSetForeground(wm_dpy, wm_gc, 0x001E2A44UL);
    XFillRectangle(wm_dpy, palette_window, wm_gc, 4, 4, PALETTE_W - 8, 24);

    if (wm_font) {
        XSetFont(wm_dpy, wm_gc, wm_font->fid);
        XSetForeground(wm_dpy, wm_gc, 0x00F0F0F0UL);
        char line[80];
        snprintf(line, sizeof(line), "> %s%s", palette_query, (palette_query[0] ? "" : "type an app name..."));
        XDrawString(wm_dpy, palette_window, wm_gc, 10, 20, line, (int)strlen(line));

        for (int r = 0; r < palette_match_count; r++) {
            int icon_idx = palette_matches[r];
            int row_y = 34 + r * PALETTE_ROW_H;
            if (r == palette_selected) {
                XSetForeground(wm_dpy, wm_gc, 0x00304870UL);
                XFillRectangle(wm_dpy, palette_window, wm_gc, 4, row_y, PALETTE_W - 8, PALETTE_ROW_H - 2);
            }
            XSetForeground(wm_dpy, wm_gc, 0x00E0E4EEUL);
            XDrawString(wm_dpy, palette_window, wm_gc, 14, row_y + 16,
                        g_icons[icon_idx].label, (int)strlen(g_icons[icon_idx].label));
        }
    }
}

static void palette_open(void)
{
    palette_create_window_if_needed();
    palette_query[0] = '\0';
    palette_selected = 0;
    palette_recompute_matches();
    XMapRaised(wm_dpy, palette_window);
    XSetInputFocus(wm_dpy, palette_window, RevertToPointerRoot, CurrentTime);
    palette_draw();
    palette_active = 1;
}

static void palette_close(void)
{
    if (!palette_window) return;
    XUnmapWindow(wm_dpy, palette_window);
    palette_active = 0;
    /* Give focus back to whatever was focused before (the desktop, if
     * nothing else) so typing doesn't vanish into an unmapped window. */
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].focused) {
            XSetInputFocus(wm_dpy, wm_windows[i].client, RevertToPointerRoot, CurrentTime);
            return;
        }
    }
    if (g_window) {
        XSetInputFocus(wm_dpy, (Window)bot_window_get_native_handle(g_window), RevertToPointerRoot, CurrentTime);
    }
}

/* launch_app() is defined later in the file (it's the same dispatcher
 * the desktop icons use); forward-declared here since the palette's
 * key handler, wired up earlier in the WM section, needs to call it. */
static void launch_app(const char *path, char *const argv[]);

static void palette_launch_selected(void)
{
    if (palette_match_count == 0) return;
    int icon_idx = palette_matches[palette_selected];
    const char *label = g_icons[icon_idx].label;

    if (strcmp(label, "Shell") == 0) { char *argv[] = { "/usr/bin/botterm", NULL }; launch_app(argv[0], argv); }
    else if (strcmp(label, "Paint") == 0) { char *argv[] = { "/usr/bin/botpaint", NULL }; launch_app(argv[0], argv); }
    else if (strcmp(label, "Editor") == 0) { char *argv[] = { "/usr/bin/editor", NULL }; launch_app(argv[0], argv); }
    else if (strcmp(label, "Files") == 0) { char *argv[] = { "/usr/bin/botfiles", NULL }; launch_app(argv[0], argv); }
    else if (strcmp(label, "BotPkg") == 0) { char *argv[] = { "/usr/bin/botpkg_gui", NULL }; launch_app(argv[0], argv); }
    else if (strcmp(label, "Settings") == 0) { char *argv[] = { "/usr/bin/botsettings", NULL }; launch_app(argv[0], argv); }
    else if (strcmp(label, "Info") == 0) { char *argv[] = { "/usr/bin/botinfo", NULL }; launch_app(argv[0], argv); }
}

static void palette_handle_key(XKeyEvent *ev)
{
    KeySym sym = XLookupKeysym(ev, 0);

    if (sym == XK_Escape) { palette_close(); return; }
    if (sym == XK_Return || sym == XK_KP_Enter) { palette_launch_selected(); palette_close(); return; }
    if (sym == XK_Down) {
        if (palette_selected < palette_match_count - 1) palette_selected++;
        palette_draw();
        return;
    }
    if (sym == XK_Up) {
        if (palette_selected > 0) palette_selected--;
        palette_draw();
        return;
    }
    if (sym == XK_BackSpace) {
        size_t len = strlen(palette_query);
        if (len > 0) palette_query[len - 1] = '\0';
        palette_selected = 0;
        palette_recompute_matches();
        palette_draw();
        return;
    }
    if (sym >= 0x20 && sym <= 0x7E) {
        size_t len = strlen(palette_query);
        if (len < sizeof(palette_query) - 1) {
            palette_query[len] = (char)sym;
            palette_query[len + 1] = '\0';
            palette_selected = 0;
            palette_recompute_matches();
            palette_draw();
        }
    }
}

/* ════════════════════════════════════════════════════════════
 *  SCREENSHOT (Ctrl+Shift+S)
 *
 *  Captures the whole screen and saves it as a 24-bit BMP — chosen
 *  deliberately over PNG/JPEG because a BMP encoder is ~40 lines of
 *  pure C with zero external dependencies, in keeping with how much
 *  of BotOS already prefers "no new dependency" over "slightly
 *  smaller output file". Confirms via the same toast notification
 *  path bot_notify_send() uses, so the two new features share one
 *  code path instead of BotDesk growing two different ways to show a
 *  popup.
 * ════════════════════════════════════════════════════════════ */

static int bmp_write(const char *path, int width, int height, const unsigned char *rgb_top_down)
{
    /* rgb_top_down: width*height*3 bytes, row-major top-to-bottom, RGB
     * byte order — converted below to BMP's actual on-disk
     * requirements (bottom-up row order, BGR byte order, each row
     * padded to a multiple of 4 bytes). */
    int row_stride = (width * 3 + 3) & ~3;
    int pixel_data_size = row_stride * height;
    int file_size = 14 + 40 + pixel_data_size;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    unsigned char file_header[14] = {
        'B', 'M',
        (unsigned char)(file_size),       (unsigned char)(file_size >> 8),
        (unsigned char)(file_size >> 16), (unsigned char)(file_size >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0 /* pixel data offset */
    };
    unsigned char info_header[40] = {0};
    info_header[0] = 40; /* header size */
    info_header[4]  = (unsigned char)(width);       info_header[5]  = (unsigned char)(width >> 8);
    info_header[6]  = (unsigned char)(width >> 16); info_header[7]  = (unsigned char)(width >> 24);
    info_header[8]  = (unsigned char)(height);      info_header[9]  = (unsigned char)(height >> 8);
    info_header[10] = (unsigned char)(height >> 16);info_header[11] = (unsigned char)(height >> 24);
    info_header[12] = 1;  /* color planes */
    info_header[14] = 24; /* bits per pixel */
    info_header[20] = (unsigned char)(pixel_data_size);
    info_header[21] = (unsigned char)(pixel_data_size >> 8);
    info_header[22] = (unsigned char)(pixel_data_size >> 16);
    info_header[23] = (unsigned char)(pixel_data_size >> 24);

    fwrite(file_header, 1, sizeof(file_header), f);
    fwrite(info_header, 1, sizeof(info_header), f);

    unsigned char *row = calloc(1, (size_t)row_stride);
    if (!row) { fclose(f); return -1; }

    for (int y = height - 1; y >= 0; y--) { /* BMP rows are bottom-up */
        const unsigned char *src_row = rgb_top_down + (size_t)y * (size_t)width * 3;
        for (int x = 0; x < width; x++) {
            row[x*3 + 0] = src_row[x*3 + 2]; /* B */
            row[x*3 + 1] = src_row[x*3 + 1]; /* G */
            row[x*3 + 2] = src_row[x*3 + 0]; /* R */
        }
        fwrite(row, 1, (size_t)row_stride, f);
    }
    free(row);
    fclose(f);
    return 0;
}

static void take_screenshot(void)
{
    int sw = DisplayWidth(wm_dpy, DefaultScreen(wm_dpy));
    int sh = DisplayHeight(wm_dpy, DefaultScreen(wm_dpy));

    XImage *img = XGetImage(wm_dpy, wm_root, 0, 0, (unsigned)sw, (unsigned)sh, AllPlanes, ZPixmap);
    if (!img) {
        toast_show("Screenshot", "Failed to capture the screen");
        return;
    }

    unsigned char *rgb = malloc((size_t)sw * (size_t)sh * 3);
    if (!rgb) {
        XDestroyImage(img);
        toast_show("Screenshot", "Out of memory");
        return;
    }

    /* Mask-based pixel extraction: correct regardless of the X
     * server's actual bits-per-pixel/byte order for this visual,
     * rather than assuming one specific layout. */
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            unsigned long pixel = XGetPixel(img, x, y);
            unsigned char r = (unsigned char)((pixel & img->red_mask)   >> 16);
            unsigned char g = (unsigned char)((pixel & img->green_mask) >> 8);
            unsigned char b = (unsigned char)(pixel & img->blue_mask);
            size_t idx = ((size_t)y * (size_t)sw + (size_t)x) * 3;
            rgb[idx] = r; rgb[idx+1] = g; rgb[idx+2] = b;
        }
    }
    XDestroyImage(img);

    const char *home = getenv("HOME");
    if (!home) home = "/root";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/Pictures/Screenshots", home);
    /* Best-effort directory creation, component by component; ignore
     * failures here and let fopen() in bmp_write() report the real
     * error if the path still isn't writable. */
    char partial[512];
    partial[0] = '\0';
    char dir_copy[512];
    strncpy(dir_copy, dir, sizeof(dir_copy) - 1);
    dir_copy[sizeof(dir_copy) - 1] = '\0';
    for (char *p = dir_copy + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - dir_copy);
            if (len < sizeof(partial)) {
                memcpy(partial, dir_copy, len);
                partial[len] = '\0';
                mkdir(partial, 0755);
            }
            if (*p == '\0') break;
        }
    }

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char path[600];
    snprintf(path, sizeof(path), "%s/Screenshot-%04d%02d%02d-%02d%02d%02d.bmp", dir,
             tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
             tmv->tm_hour, tmv->tm_min, tmv->tm_sec);

    int rc = bmp_write(path, sw, sh, rgb);
    free(rgb);

    if (rc == 0) {
        char body[650];
        snprintf(body, sizeof(body), "Saved to %s", path);
        toast_show("Screenshot captured", body);
    } else {
        toast_show("Screenshot", "Failed to save file");
    }
}

static void wm_handle_key_press(XKeyEvent *ev)
{
    if (palette_active) {
        palette_handle_key(ev);
        return;
    }

    KeySym sym = XLookupKeysym(ev, 0);
    if (sym == XK_Tab && (ev->state & Mod1Mask)) {
        wm_cycle_focus(!(ev->state & ShiftMask));
        return;
    }
    if (sym == XK_space && (ev->state & ControlMask)) {
        palette_open();
        return;
    }
    if ((sym == XK_s || sym == XK_S) && (ev->state & ControlMask) && (ev->state & ShiftMask)) {
        take_screenshot();
        return;
    }
}

static void wm_handle_expose_ext(XExposeEvent *ev)
{
    if (toast_window && ev->window == toast_window) { toast_draw(); return; }
    if (palette_window && ev->window == palette_window) { palette_draw(); return; }
    wm_handle_expose(ev);
}

static void wm_pump_events(void)
{
    if (!wm_active || !wm_dpy) return;

    notify_poll();
    toast_check_expiry();

    while (XPending(wm_dpy)) {
        XEvent ev;
        XNextEvent(wm_dpy, &ev);
        switch (ev.type) {
            case MapRequest:       wm_handle_map_request(&ev.xmaprequest);       break;
            case ConfigureRequest: wm_handle_configure_request(&ev.xconfigurerequest); break;
            case DestroyNotify:    wm_handle_destroy_notify(&ev.xdestroywindow); break;
            case UnmapNotify:      wm_handle_unmap_notify(&ev.xunmap);           break;
            case Expose:           wm_handle_expose_ext(&ev.xexpose);            break;
            case ButtonPress:      wm_handle_button_press(&ev.xbutton);          break;
            case ButtonRelease:    wm_handle_button_release(&ev.xbutton);        break;
            case MotionNotify:     wm_handle_motion(&ev.xmotion);                break;
            case KeyPress:         wm_handle_key_press(&ev.xkey);                break;
            default: break;
        }
    }
}

static void wm_init(void)
{
    wm_dpy = XOpenDisplay(NULL);
    if (!wm_dpy) { wm_active = 0; return; }
    wm_root = DefaultRootWindow(wm_dpy);

    /* Claiming SubstructureRedirect is how an X11 client becomes THE
     * window manager — and it fails with BadAccess if one is already
     * running. Use a temporary error handler to detect that without
     * crashing, exactly like every other X window manager's startup
     * path does. */
    XSetErrorHandler(wm_temp_error_handler);
    wm_xerror_occurred = 0;
    XSelectInput(wm_dpy, wm_root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);
    XSync(wm_dpy, False);

    if (wm_xerror_occurred) {
        BOT_LOG_WARN("Another window manager already owns this display — "
                      "BotDesk will launch apps fullscreen, one at a time.");
        XCloseDisplay(wm_dpy);
        wm_dpy = NULL;
        wm_active = 0;
        return;
    }
    XSetErrorHandler(wm_runtime_error_handler);

    wm_atom_delete    = XInternAtom(wm_dpy, "WM_DELETE_WINDOW", False);
    wm_atom_protocols = XInternAtom(wm_dpy, "WM_PROTOCOLS", False);

    /* Alt+Tab / Alt+Shift+Tab to cycle window focus. Grabbed on the
     * root window so it works no matter which client has input focus.
     * Also grab with LockMask/Mod2Mask (Caps Lock/Num Lock) added in
     * since X treats those as just more modifier bits — without the
     * extra combinations the grab silently stops matching the moment
     * either lock key is toggled on. */
    {
        unsigned int ignore_masks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
        KeyCode tab_code = XKeysymToKeycode(wm_dpy, XK_Tab);
        if (tab_code) {
            for (size_t i = 0; i < sizeof(ignore_masks) / sizeof(ignore_masks[0]); i++) {
                XGrabKey(wm_dpy, tab_code, Mod1Mask | ignore_masks[i],
                          wm_root, True, GrabModeAsync, GrabModeAsync);
                XGrabKey(wm_dpy, tab_code, Mod1Mask | ShiftMask | ignore_masks[i],
                          wm_root, True, GrabModeAsync, GrabModeAsync);
            }
        }

        /* Ctrl+Space opens the command palette — same ignore-mask
         * treatment for Caps/Num Lock as Alt+Tab above. */
        KeyCode space_code = XKeysymToKeycode(wm_dpy, XK_space);
        if (space_code) {
            for (size_t i = 0; i < sizeof(ignore_masks) / sizeof(ignore_masks[0]); i++) {
                XGrabKey(wm_dpy, space_code, ControlMask | ignore_masks[i],
                          wm_root, True, GrabModeAsync, GrabModeAsync);
            }
        }

        /* Ctrl+Shift+S takes a screenshot. */
        KeyCode s_code = XKeysymToKeycode(wm_dpy, XK_s);
        if (s_code) {
            for (size_t i = 0; i < sizeof(ignore_masks) / sizeof(ignore_masks[0]); i++) {
                XGrabKey(wm_dpy, s_code, ControlMask | ShiftMask | ignore_masks[i],
                          wm_root, True, GrabModeAsync, GrabModeAsync);
            }
        }
    }

    wm_gc = XCreateGC(wm_dpy, wm_root, 0, NULL);
    wm_font = XLoadQueryFont(wm_dpy, "fixed");
    if (!wm_font) wm_font = XLoadQueryFont(wm_dpy, "*");

    wm_active = 1;
    BOT_LOG_INFO("BotDesk window manager active (multi-window mode).");
}

static void wm_shutdown(void)
{
    if (!wm_dpy) return;
    if (wm_font) XFreeFont(wm_dpy, wm_font);
    if (wm_gc)   XFreeGC(wm_dpy, wm_gc);
    XCloseDisplay(wm_dpy);
    wm_dpy = NULL;
    wm_active = 0;
}

#endif /* BOTOS_HAS_X11 */



static double get_cpu_usage(void)
{
    static long double a[4], b[4];
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;
    
    char cpu[10];
    if (fscanf(fp, "%s %Lf %Lf %Lf %Lf", cpu, &b[0], &b[1], &b[2], &b[3]) != 5) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);
    
    long double load = (b[0] + b[1] + b[2]) - (a[0] + a[1] + a[2]);
    long double total = (b[0] + b[1] + b[2] + b[3]) - (a[0] + a[1] + a[2] + a[3]);
    
    a[0] = b[0]; a[1] = b[1]; a[2] = b[2]; a[3] = b[3];
    
    if (total == 0) return 0.0;
    double usage = (double)(load / total) * 100.0;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return usage;
}

static double get_ram_usage(void)
{
    long total_mem = 0, free_mem = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0.0;
    
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        long val;
        if (sscanf(line, "MemTotal: %ld kB", &val) == 1) total_mem = val;
        else if (sscanf(line, "MemFree: %ld kB", &val) == 1) free_mem = val;
    }
    fclose(fp);
    
    if (total_mem == 0) return 0.0;
    long used_mem = total_mem - free_mem;
    double usage = (double)used_mem / (double)total_mem * 100.0;
    return usage;
}

static void draw_system_widget(void)
{
    int wx = 800;
    int wy = 20;
    int ww = 204;
    int wh = 80;

    bot_theme_t *th = bot_theme_get();

    bot_draw_panel(g_canvas, wx, wy, ww, wh);
    bot_canvas_draw_rect(g_canvas, wx, wy, ww, wh, th->panel_border);

    bot_draw_text(g_canvas, wx + 8, wy + 6, "SYSTEM MONITOR", th->fg, 1);
    bot_draw_separator(g_canvas, wx, wy + 16, ww);

    double cpu = get_cpu_usage();
    double ram = get_ram_usage();

    char cpu_txt[32];
    snprintf(cpu_txt, sizeof(cpu_txt), "CPU: %3.0f%%", cpu);
    bot_draw_text(g_canvas, wx + 8, wy + 24, cpu_txt, th->fg, 1);
    
    int bar_w = 120;
    int bar_h = 8;
    int bar_x = wx + 70;
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 24, bar_w, bar_h, th->btn_normal);
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 24, (int)(bar_w * cpu / 100.0), bar_h, th->accent);

    char ram_txt[32];
    snprintf(ram_txt, sizeof(ram_txt), "RAM: %3.0f%%", ram);
    bot_draw_text(g_canvas, wx + 8, wy + 42, ram_txt, th->fg, 1);
    
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 42, bar_w, bar_h, th->btn_normal);
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 42, (int)(bar_w * ram / 100.0), bar_h, th->accent);
    
    double uptime_secs = 0.0;
    FILE *uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file) {
        if (fscanf(uptime_file, "%lf", &uptime_secs) != 1) uptime_secs = 0.0;
        fclose(uptime_file);
    }
    int uptime_h = (int)(uptime_secs / 3600);
    int uptime_m = (int)((uptime_secs - uptime_h * 3600) / 60);
    char uptime_txt[64];
    snprintf(uptime_txt, sizeof(uptime_txt), "Uptime: %dh %dm", uptime_h, uptime_m);
    bot_draw_text(g_canvas, wx + 8, wy + 62, uptime_txt, th->fg_dim, 1);
}

static void draw_wallpaper(void)
{
    bot_theme_t *th = bot_theme_get();
    if (th->bg == 0xFF000000) {
        bot_canvas_clear(g_canvas, 0xFF000000);
    } else if (th->bg == 0xFFF0F0F0) {
        bot_draw_gradient_v(g_canvas, 0, 0, DESK_WIDTH, TASKBAR_Y,
                            BOT_RGBA(230, 235, 250, 255), BOT_RGBA(190, 200, 230, 255));
    } else {
        bot_draw_gradient_v(g_canvas, 0, 0, DESK_WIDTH, TASKBAR_Y,
                            COL_BG_TOP, COL_BG_BOT);
    }
}

/* ── Desktop State ───────────────────────────────────────── */

static int           g_menu_hover  = 0;
static int           g_desktop_mouse_x = 0;
static int           g_desktop_mouse_y = 0;
static uint32_t      g_last_click_time = 0;
static int           g_last_clicked_icon = -1;

static int           g_show_alert = 0;
static char          g_alert_message[128] = "";

static uint32_t get_ticks_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── Layout ──────────────────────────────────────────────── */

static void layout_icons(void)
{
    for (int i = 0; i < ICON_COUNT; i++) {
        g_icons[i].x = ICON_GRID_X + (i % 2) * (ICON_SIZE + ICON_SPACING);
        g_icons[i].y = ICON_GRID_Y + (i / 2) * (ICON_SIZE + ICON_SPACING);
    }
}

/* ── Rendering ───────────────────────────────────────────── */

static void render_desktop(void)
{
    bot_theme_load();
    draw_wallpaper();

    /* Desktop icons via widget toolkit */
    for (int i = 0; i < ICON_COUNT; i++) {
        int x1 = g_icons[i].x;
        int y1 = g_icons[i].y;
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20;

        int hovered = (g_desktop_mouse_x >= x1 && g_desktop_mouse_x < x2 &&
                       g_desktop_mouse_y >= y1 && g_desktop_mouse_y < y2);

        /* Draw a subtle background highlight if hovered */
        if (hovered) {
            bot_canvas_fill_rect(g_canvas, x1 - 6, y1 - 6, ICON_SIZE + 12, ICON_SIZE + 28,
                                 BOT_RGBA(255, 255, 255, 30));
        }

        bot_draw_icon(g_canvas, g_icons[i].x, g_icons[i].y,
                      ICON_SIZE, g_icons[i].icon_color, g_icons[i].label);
    }

    /* Draw System Monitor Widget */
    draw_system_widget();

    /* Taskbar panel */
    bot_draw_panel(g_canvas, 0, TASKBAR_Y, DESK_WIDTH, TASKBAR_HEIGHT);
    bot_draw_separator(g_canvas, 0, TASKBAR_Y, DESK_WIDTH);

    /* Menu button */
    bot_btn_state_t menu_state = g_menu_hover ? BOT_BTN_HOVER : BOT_BTN_NORMAL;
    bot_draw_button(g_canvas, 2, TASKBAR_Y + 2, MENU_BTN_WIDTH, MENU_BTN_HEIGHT,
                    "MENU", menu_state);

    /* BotOS logo circle on menu button */
    bot_theme_t *th = bot_theme_get();
    bot_canvas_draw_circle(g_canvas, 16, TASKBAR_Y + TASKBAR_HEIGHT / 2,
                           8, th->accent);

    /* Clock area */
    int clock_x = DESK_WIDTH - CLOCK_WIDTH;
    bot_draw_button(g_canvas, clock_x, TASKBAR_Y + 2,
                    CLOCK_WIDTH - 2, MENU_BTN_HEIGHT, NULL, BOT_BTN_NORMAL);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm->tm_hour, tm->tm_min);
    bot_draw_text(g_canvas, clock_x + 14, TASKBAR_Y + 10,
                  time_str, th->fg, 2);

    /* Window list: real open windows when we're acting as the WM,
     * otherwise the static label this always used to show. */
#ifdef BOTOS_HAS_X11
    if (wm_active && wm_window_count > 0) {
        int tx = MENU_BTN_WIDTH + 8;
        for (int i = 0; i < WM_MAX_WINDOWS && tx + WM_TASKBAR_BTN_W < clock_x - 8; i++) {
            if (!wm_windows[i].used) continue;
            bot_btn_state_t st = wm_windows[i].focused ? BOT_BTN_ACTIVE : BOT_BTN_NORMAL;
            bot_draw_button(g_canvas, tx, TASKBAR_Y + 2, WM_TASKBAR_BTN_W - 6,
                            MENU_BTN_HEIGHT, wm_windows[i].title, st);
            tx += WM_TASKBAR_BTN_W;
        }
    } else {
        bot_draw_text(g_canvas, MENU_BTN_WIDTH + 16, TASKBAR_Y + 13,
                      "BOTDESK", th->fg_dim, 1);
    }
#else
    bot_draw_text(g_canvas, MENU_BTN_WIDTH + 16, TASKBAR_Y + 13,
                  "BOTDESK", th->fg_dim, 1);
#endif

    /* Draw alert modal if active */
    if (g_show_alert) {
        int box_w = 400;
        int box_h = 120;
        int box_x = (DESK_WIDTH - box_w) / 2;
        int box_y = (DESK_HEIGHT - box_h) / 2;

        /* Dim the screen background */
        bot_canvas_fill_rect(g_canvas, 0, 0, DESK_WIDTH, TASKBAR_Y, BOT_RGBA(0, 0, 0, 100));

        /* Draw panel and decoration */
        bot_draw_panel(g_canvas, box_x, box_y, box_w, box_h);
        bot_draw_window_frame(g_canvas, box_x, box_y, box_w, box_h, "Bilgi", 24);

        /* Center and draw text message */
        int text_w = bot_measure_text(g_alert_message, 1);
        int tx = box_x + (box_w - text_w) / 2;
        bot_draw_text(g_canvas, tx, box_y + 48, g_alert_message, th->fg, 1);

        /* Draw OK button */
        int btn_w = 60;
        int btn_h = 24;
        int btn_x = box_x + (box_w - btn_w) / 2;
        int btn_y = box_y + box_h - 36;
        
        bot_draw_button(g_canvas, btn_x, btn_y, btn_w, btn_h, "Tamam", BOT_BTN_NORMAL);
    }

    /* Blit canvas → framebuffer */
    uint32_t *fb = bot_window_get_framebuffer(g_window);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    if (fb && px)
        memcpy(fb, px, (size_t)(DESK_WIDTH * DESK_HEIGHT) * sizeof(uint32_t));
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

/* ── Event Handlers ──────────────────────────────────────── */

static void reap_children(void)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {
        /* nothing to do — just prevent zombies from piling up */
    }
}

/* Original behavior: used when BotDesk isn't acting as the window
 * manager (framebuffer backend, or another WM already running). On a
 * bare framebuffer there's exactly one physical screen and no
 * compositing, so only one app can plausibly be shown at a time. */
static void launch_app_fullscreen(const char *path, char *const argv[])
{
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();

    pid_t pid = fork();
    if (pid == 0) {
        execvp(path, argv);
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }

    bot_ui_init();
    g_window = bot_window_create("BotDesk", DESK_WIDTH, DESK_HEIGHT);
    g_canvas = bot_canvas_create(DESK_WIDTH, DESK_HEIGHT);
    if (g_window) bot_window_show(g_window);
    g_needs_redraw = 1;
}

#ifdef BOTOS_HAS_X11
/* Real multi-window behavior: fork the app and keep going. Its own
 * bot_window_create()/bot_window_show() call will generate a
 * MapRequest that wm_handle_map_request() decorates and manages, so
 * it shows up as its own movable window while BotDesk's desktop and
 * taskbar keep running underneath. */
static void launch_app_windowed(const char *path, char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp(path, argv);
        _exit(127);
    }
    /* Parent keeps going; reap_children() cleans up on exit. */
}
#endif

static void launch_app(const char *path, char *const argv[])
{
#ifdef BOTOS_HAS_X11
    if (wm_active) {
        launch_app_windowed(path, argv);
        return;
    }
#endif
    launch_app_fullscreen(path, argv);
}

static void on_mouse_down(const bot_event_t *event, void *data)
{
    (void)data;
    int mx = event->mouse.x;
    int my = event->mouse.y;
    uint32_t now = get_ticks_ms();

    if (g_show_alert) {
        int box_w = 400;
        int box_h = 120;
        int box_x = (DESK_WIDTH - box_w) / 2;
        int box_y = (DESK_HEIGHT - box_h) / 2;
        int btn_w = 60;
        int btn_h = 24;
        int btn_x = box_x + (box_w - btn_w) / 2;
        int btn_y = box_y + box_h - 36;

        if (mx >= btn_x && mx < btn_x + btn_w && my >= btn_y && my < btn_y + btn_h) {
            g_show_alert = 0;
            g_needs_redraw = 1;
        }
        return;
    }

#ifdef BOTOS_HAS_X11
    /* Taskbar window-list buttons: click to raise + focus. */
    if (wm_active && wm_window_count > 0) {
        int clock_x = DESK_WIDTH - CLOCK_WIDTH;
        int tx = MENU_BTN_WIDTH + 8;
        for (int i = 0; i < WM_MAX_WINDOWS && tx + WM_TASKBAR_BTN_W < clock_x - 8; i++) {
            if (!wm_windows[i].used) continue;
            if (bot_button_hit(tx, TASKBAR_Y + 2, WM_TASKBAR_BTN_W - 6, MENU_BTN_HEIGHT, mx, my)) {
                wm_set_focus(&wm_windows[i]);
                return;
            }
            tx += WM_TASKBAR_BTN_W;
        }
    }
#endif

    /* Check icons */
    for (int i = 0; i < ICON_COUNT; i++) {
        int x1 = g_icons[i].x;
        int y1 = g_icons[i].y;
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20; /* Expands to cover the text label */

        if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
            /* Check if it is a double click on the same icon */
            if (g_last_clicked_icon == i && (now - g_last_click_time) < 500) {
                /* Double click detected! Launch app */
                g_last_clicked_icon = -1; /* Reset */
                if (strcmp(g_icons[i].label, "Shell") == 0) {
                    char *argv[] = { "/usr/bin/botterm", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Paint") == 0) {
                    char *argv[] = { "/usr/bin/botpaint", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Editor") == 0) {
                    char *argv[] = { "/usr/bin/editor", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Files") == 0) {
                    char *argv[] = { "/usr/bin/botfiles", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "BotPkg") == 0) {
                    char *argv[] = { "/usr/bin/botpkg_gui", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Settings") == 0) {
                    char *argv[] = { "/usr/bin/botsettings", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Info") == 0) {
                    char *argv[] = { "/usr/bin/botinfo", NULL };
                    launch_app(argv[0], argv);
                } else {
                    snprintf(g_alert_message, sizeof(g_alert_message), "%s uygulamasi yakinda eklenecektir!", g_icons[i].label);
                    g_show_alert = 1;
                    g_needs_redraw = 1;
                }
            } else {
                g_last_clicked_icon = i;
                g_last_click_time = now;
            }
            break;
        }
    }
}

static void on_mouse_move(const bot_event_t *event, void *data)
{
    (void)data;
    g_desktop_mouse_x = event->mouse.x;
    g_desktop_mouse_y = event->mouse.y;
    g_menu_hover = bot_button_hit(2, TASKBAR_Y + 2, MENU_BTN_WIDTH,
                                  MENU_BTN_HEIGHT, g_desktop_mouse_x, g_desktop_mouse_y);
    g_needs_redraw = 1;
}

static void on_key_down(const bot_event_t *event, void *data)
{
    (void)data;
    if ((event->key.keycode == 'q' && (event->key.modifiers & 2)) ||
        event->key.keycode == BOT_KEY_ESCAPE) {
        bot_event_quit();
    }
}

static void on_resize(const bot_event_t *event, void *data)
{
    (void)event; (void)data;
    g_needs_redraw = 1;
}

/* ── Main ────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    bot_log_init(NULL, BOT_LOG_INFO);
    BOT_LOG_INFO("BotDesk starting...");

    if (bot_ui_init() != 0) {
        BOT_LOG_FATAL("Failed to initialize UI subsystem");
        return 1;
    }

    g_window = bot_window_create("BotDesk", DESK_WIDTH, DESK_HEIGHT);
    g_canvas = bot_canvas_create(DESK_WIDTH, DESK_HEIGHT);
    if (!g_window || !g_canvas) {
        BOT_LOG_FATAL("Failed to create desktop window/canvas");
        bot_ui_shutdown();
        return 1;
    }

    layout_icons();

#ifdef BOTOS_HAS_X11
    wm_init();
#endif

    bot_event_on(BOT_EVENT_MOUSE_MOVE, on_mouse_move, NULL);
    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_KEY_DOWN,   on_key_down,   NULL);
    bot_event_on(BOT_EVENT_RESIZE,     on_resize,     NULL);

    bot_window_show(g_window);
    render_desktop();

    bot_event_t event;
    uint32_t last_update = 0;
    while (!bot_window_should_close(g_window)) {
        uint32_t now = get_ticks_ms();
        if (now - last_update >= 2000) {
            g_needs_redraw = 1;
            last_update = now;
        }

        while (bot_event_poll(&event)) {
            if (event.type == BOT_EVENT_CLOSE) goto cleanup;
        }

#ifdef BOTOS_HAS_X11
        wm_pump_events();
#endif
        reap_children();

        if (g_needs_redraw) {
            render_desktop();
        }

        usleep(50000);
    }

cleanup:
#ifdef BOTOS_HAS_X11
    wm_shutdown();
#endif
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    bot_log_shutdown();
    return 0;
}
