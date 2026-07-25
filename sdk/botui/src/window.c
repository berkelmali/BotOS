/* ============================================================
 * BotOS Core — Window Manager (Production)
 * ============================================================
 * File:    window.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — Production window management.
 *
 * Dual backend architecture:
 *
 *   1. X11 BACKEND (BOTOS_HAS_X11):
 *      Creates native X11 windows via Xlib. Uses XImage
 *      backed by a shared framebuffer for zero-copy blitting.
 *      Handles WM_DELETE_WINDOW for graceful close.
 *
 *   2. FRAMEBUFFER BACKEND (default):
 *      Pure software framebuffer for headless/embedded use.
 *      Suitable for testing, CI, and VNC-based remote display.
 *      All drawing ops work identically; flip() is a no-op.
 *
 * Window features:
 *   - Configurable title, size, visibility
 *   - Framebuffer resize with content preservation
 *   - Dirty-rect tracking for partial flush optimization
 *   - Thread-safe close flag (volatile)
 * ============================================================ */

#include "bot_window.h"
#include "bot_canvas.h"
#include "bot_event.h"
#include "bot_widget.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bot_window_t *g_active_window = NULL;

/* ════════════════════════════════════════════════════════════
 *  X11 BACKEND
 * ════════════════════════════════════════════════════════════ */

#ifdef BOTOS_HAS_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

/* ── X11 Global State ────────────────────────────────────── */

/* External linkage: event.c declares these `extern` to pump the X11
 * event queue against the same display connection this file opens. */
Display *g_display = NULL;
static int      g_screen  = 0;
Atom     g_wm_delete_message;
static int      g_display_refcount = 0;

/**
 * Initialize the X11 display connection (shared singleton).
 */
static int x11_ensure_display(void)
{
    if (g_display) {
        g_display_refcount++;
        return 0;
    }

    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        fprintf(stderr, "[botui] Cannot open X11 display\n");
        return -1;
    }

    g_screen = DefaultScreen(g_display);
    g_wm_delete_message = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    g_display_refcount = 1;

    return 0;
}

static void x11_release_display(void)
{
    g_display_refcount--;
    if (g_display_refcount <= 0 && g_display) {
        XCloseDisplay(g_display);
        g_display = NULL;
        g_display_refcount = 0;
    }
}

/* ── Window Structure (X11) ──────────────────────────────── */

struct bot_window {
    char      title[256];
    int       width;
    int       height;
    int       visible;
    volatile int should_close;

    uint32_t *framebuffer;

    /* X11-specific */
    Window    xwin;
    GC        gc;
    XImage   *ximage;
    int       depth;

    /* Window manager state */
    int       title_h;
    int       orig_width;
    int       orig_height;
    int       is_maximized;
};

/* ── Lifecycle ───────────────────────────────────────────── */

int bot_ui_init(void)
{
    bot_theme_load();
    return x11_ensure_display();
}

void bot_ui_shutdown(void)
{
    x11_release_display();
}

bot_window_t *bot_window_create(const char *title, int width, int height)
{
    if (width <= 0 || height <= 0) return NULL;

    if (x11_ensure_display() != 0) return NULL;

    bot_window_t *win = (bot_window_t *)calloc(1, sizeof(bot_window_t));
    if (!win) return NULL;

    if (title) strncpy(win->title, title, sizeof(win->title) - 1);
    win->width  = width;
    win->height = height;
    win->depth  = DefaultDepth(g_display, g_screen);
    win->title_h = 24;
    win->orig_width = width;
    win->orig_height = height;
    win->is_maximized = 0;

    g_active_window = win;

    /* Allocate framebuffer */
    win->framebuffer = (uint32_t *)calloc((size_t)(width * height), sizeof(uint32_t));
    if (!win->framebuffer) {
        free(win);
        return NULL;
    }

    /* Create X11 window */
    win->xwin = XCreateSimpleWindow(
        g_display,
        RootWindow(g_display, g_screen),
        0, 0, (unsigned)width, (unsigned)height,
        1,
        BlackPixel(g_display, g_screen),
        BlackPixel(g_display, g_screen)
    );

    /* Register for WM_DELETE_WINDOW */
    XSetWMProtocols(g_display, win->xwin, &g_wm_delete_message, 1);

    /* Select input events */
    XSelectInput(g_display, win->xwin,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | StructureNotifyMask);

    /* Set title */
    XStoreName(g_display, win->xwin, win->title);

    /* Create GC */
    win->gc = XCreateGC(g_display, win->xwin, 0, NULL);

    /* Create XImage backed by our framebuffer */
    Visual *visual = DefaultVisual(g_display, g_screen);
    win->ximage = XCreateImage(
        g_display, visual, (unsigned)win->depth,
        ZPixmap, 0, (char *)win->framebuffer,
        (unsigned)width, (unsigned)height,
        32, 0
    );

    return win;
}

void bot_window_show(bot_window_t *win)
{
    if (!win || !g_display) return;
    XMapWindow(g_display, win->xwin);
    XFlush(g_display);
    win->visible = 1;
}

void bot_window_hide(bot_window_t *win)
{
    if (!win || !g_display) return;
    XUnmapWindow(g_display, win->xwin);
    XFlush(g_display);
    win->visible = 0;
}

void bot_window_set_title(bot_window_t *win, const char *title)
{
    if (!win || !title || !g_display) return;
    strncpy(win->title, title, sizeof(win->title) - 1);
    XStoreName(g_display, win->xwin, win->title);
    XFlush(g_display);
}

void bot_window_resize(bot_window_t *win, int width, int height)
{
    if (!win || width <= 0 || height <= 0) return;

    /* Allocate new framebuffer */
    uint32_t *new_fb = (uint32_t *)calloc((size_t)(width * height), sizeof(uint32_t));
    if (!new_fb) return;

    /* Copy preserved content (smaller of old/new dimensions) */
    int copy_w = width < win->width ? width : win->width;
    int copy_h = height < win->height ? height : win->height;

    for (int y = 0; y < copy_h; y++) {
        memcpy(new_fb + y * width,
               win->framebuffer + y * win->width,
               (size_t)copy_w * sizeof(uint32_t));
    }

    /* Destroy old XImage (but NOT the data — it will be freed separately) */
    if (win->ximage) {
        win->ximage->data = NULL;  /* Prevent XDestroyImage from freeing our buffer */
        XDestroyImage(win->ximage);
    }

    free(win->framebuffer);
    win->framebuffer = new_fb;
    win->width  = width;
    win->height = height;

    /* Recreate XImage with new buffer */
    if (g_display) {
        Visual *visual = DefaultVisual(g_display, g_screen);
        win->ximage = XCreateImage(
            g_display, visual, (unsigned)win->depth,
            ZPixmap, 0, (char *)win->framebuffer,
            (unsigned)width, (unsigned)height,
            32, 0
        );

        XResizeWindow(g_display, win->xwin, (unsigned)width, (unsigned)height);
    }
}

uint32_t *bot_window_get_framebuffer(bot_window_t *win)
{
    return win ? win->framebuffer : NULL;
}

void bot_window_flip(bot_window_t *win)
{
    if (!win || !win->visible || !g_display || !win->ximage) return;

    XPutImage(g_display, win->xwin, win->gc, win->ximage,
              0, 0, 0, 0, (unsigned)win->width, (unsigned)win->height);
    XFlush(g_display);
}

void bot_window_destroy(bot_window_t *win)
{
    if (!win) return;

    if (g_active_window == win) {
        g_active_window = NULL;
    }

    if (g_display) {
        if (win->ximage) {
            win->ximage->data = NULL;  /* Don't let XDestroyImage free our buffer */
            XDestroyImage(win->ximage);
        }
        if (win->gc) XFreeGC(g_display, win->gc);
        XDestroyWindow(g_display, win->xwin);
        XFlush(g_display);
        x11_release_display();
    }

    free(win->framebuffer);
    free(win);
}

void bot_window_get_screen_size(int *w, int *h)
{
    if (g_display) {
        if (w) *w = DisplayWidth(g_display, g_screen);
        if (h) *h = DisplayHeight(g_display, g_screen);
    } else {
        if (w) *w = 1024;
        if (h) *h = 768;
    }
}

int bot_window_should_close(const bot_window_t *win)
{
    return win ? win->should_close : 1;
}

unsigned long bot_window_get_native_handle(bot_window_t *win)
{
    return win ? (unsigned long)win->xwin : 0;
}

#endif /* BOTOS_HAS_X11 */

/* ════════════════════════════════════════════════════════════
 *  FRAMEBUFFER-ONLY BACKEND (headless / embedded)
 * ════════════════════════════════════════════════════════════ */

#ifndef BOTOS_HAS_X11

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

/* Global framebuffer state */
static int g_fb_fd = -1;
static uint32_t *g_fb_ptr = MAP_FAILED;
static int g_fb_width = 1024;
static int g_fb_height = 768;
static int g_fb_stride = 1024;
static long g_fb_screensize = 0;

extern void bot_event_get_mouse_pos(int *x, int *y);

struct bot_window {
    char      title[256];
    int       width;
    int       height;
    int       visible;
    volatile int should_close;

    uint32_t *framebuffer;

    /* Dirty rect tracking for future remote display */
    int       dirty_x1, dirty_y1;
    int       dirty_x2, dirty_y2;
    int       is_dirty;

    /* Window manager state */
    int       title_h;
    int       orig_width;
    int       orig_height;
    int       is_maximized;
};

extern void bot_event_init_devs(void);
extern void bot_event_shutdown_devs(void);

int bot_ui_init(void)
{
    bot_theme_load();
    bot_event_init_devs();
    g_fb_fd = open("/dev/fb0", O_RDWR);
    if (g_fb_fd < 0) {
        /* Headless mode fallback */
        return 0;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
        return 0;
    }

    g_fb_width = (int)vinfo.xres;
    g_fb_height = (int)vinfo.yres;
    g_fb_stride = (int)finfo.line_length / 4;
    g_fb_screensize = finfo.line_length * vinfo.yres;

    g_fb_ptr = (uint32_t *)mmap(NULL, (size_t)g_fb_screensize, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb_ptr == MAP_FAILED) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }
    return 0;
}

void bot_ui_shutdown(void)
{
    bot_event_shutdown_devs();
    if (g_fb_ptr != MAP_FAILED) {
        munmap(g_fb_ptr, (size_t)g_fb_screensize);
        g_fb_ptr = MAP_FAILED;
    }
    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }
}

bot_window_t *bot_window_create(const char *title, int width, int height)
{
    if (width <= 0 || height <= 0) return NULL;

    bot_window_t *win = (bot_window_t *)calloc(1, sizeof(bot_window_t));
    if (!win) return NULL;

    if (title) strncpy(win->title, title, sizeof(win->title) - 1);
    win->width  = width;
    win->height = height;
    win->visible = 0;
    win->should_close = 0;
    win->title_h = 24;
    win->orig_width = width;
    win->orig_height = height;
    win->is_maximized = 0;

    g_active_window = win;

    win->framebuffer = (uint32_t *)calloc((size_t)(width * height), sizeof(uint32_t));
    if (!win->framebuffer) {
        free(win);
        return NULL;
    }

    /* Initialize dirty rect to full window */
    win->dirty_x1 = 0;
    win->dirty_y1 = 0;
    win->dirty_x2 = width;
    win->dirty_y2 = height;
    win->is_dirty  = 1;

    return win;
}

void bot_window_show(bot_window_t *win)
{
    if (!win) return;
    win->visible = 1;
}

void bot_window_hide(bot_window_t *win)
{
    if (!win) return;
    win->visible = 0;
}

void bot_window_set_title(bot_window_t *win, const char *title)
{
    if (!win || !title) return;
    strncpy(win->title, title, sizeof(win->title) - 1);
}

void bot_window_resize(bot_window_t *win, int width, int height)
{
    if (!win || width <= 0 || height <= 0) return;

    uint32_t *new_fb = (uint32_t *)calloc((size_t)(width * height), sizeof(uint32_t));
    if (!new_fb) return;

    /* Preserve existing content */
    int copy_w = width < win->width ? width : win->width;
    int copy_h = height < win->height ? height : win->height;

    for (int y = 0; y < copy_h; y++) {
        memcpy(new_fb + y * width,
               win->framebuffer + y * win->width,
               (size_t)copy_w * sizeof(uint32_t));
    }

    free(win->framebuffer);
    win->framebuffer = new_fb;
    win->width  = width;
    win->height = height;

    /* Mark full window dirty */
    win->dirty_x1 = 0;
    win->dirty_y1 = 0;
    win->dirty_x2 = width;
    win->dirty_y2 = height;
    win->is_dirty  = 1;
}

uint32_t *bot_window_get_framebuffer(bot_window_t *win)
{
    return win ? win->framebuffer : NULL;
}

static void draw_fb_cursor(uint32_t *fb, int fb_w, int fb_h, int mx, int my)
{
    const int cursor_h = 16;
    const int cursor_w = 11;
    static const char *cursor_bitmap[] = {
        "X..........",
        "XX.........",
        "XXX........",
        "XXXX.......",
        "XXXXX......",
        "XXXXXX.....",
        "XXXXXXX....",
        "XXXXXXXX...",
        "XXXXXXXXX..",
        "XXXXXXXXXX.",
        "XXXXXX.....",
        "XX.XXX.....",
        "X..XXX.....",
        "...XX......",
        "...XX......",
        "....X......"
    };

    for (int y = 0; y < cursor_h; y++) {
        int sy = my + y;
        if (sy < 0 || sy >= fb_h) continue;
        for (int x = 0; x < cursor_w; x++) {
            int sx = mx + x;
            if (sx < 0 || sx >= fb_w) continue;
            char pixel = cursor_bitmap[y][x];
            if (pixel == 'X') {
                fb[sy * g_fb_stride + sx] = 0xFFFFFFFF; // White
            } else if (pixel == '.') {
                fb[sy * g_fb_stride + sx] = 0xFF000000; // Black outline
            }
        }
    }
}

void bot_window_flip(bot_window_t *win)
{
    if (!win) return;
    win->is_dirty = 0;

    if (g_fb_ptr != MAP_FAILED && win->framebuffer) {
        /* Clear screen first if the window is smaller to prevent mouse trails */
        if (win->width < g_fb_width || win->height < g_fb_height) {
            memset(g_fb_ptr, 0, (size_t)g_fb_screensize);
        }

        /* Blit window to centered screen area */
        int dx = (g_fb_width - win->width) / 2;
        int dy = (g_fb_height - win->height) / 2;
        if (dx < 0) dx = 0;
        if (dy < 0) dy = 0;

        int copy_w = win->width < g_fb_width ? win->width : g_fb_width;
        int copy_h = win->height < g_fb_height ? win->height : g_fb_height;

        for (int y = 0; y < copy_h; y++) {
            uint32_t *src = win->framebuffer + y * win->width;
            uint32_t *dst = g_fb_ptr + (y + dy) * g_fb_stride + dx;
            memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
        }

        /* Draw cursor on screen */
        int mx = 512, my = 384;
        bot_event_get_mouse_pos(&mx, &my);
        draw_fb_cursor(g_fb_ptr, g_fb_width, g_fb_height, mx, my);
    }
}

void bot_window_destroy(bot_window_t *win)
{
    if (!win) return;
    if (g_active_window == win) {
        g_active_window = NULL;
    }
    free(win->framebuffer);
    free(win);
}

int bot_window_should_close(const bot_window_t *win)
{
    return win ? win->should_close : 1;
}

unsigned long bot_window_get_native_handle(bot_window_t *win)
{
    (void)win;
    return 0; /* no native window handle exists on the framebuffer backend */
}

void bot_window_get_screen_size(int *w, int *h)
{
    if (w) *w = g_fb_width;
    if (h) *h = g_fb_height;
}

#endif /* !BOTOS_HAS_X11 */

/* ── Unified Helpers ─────────────────────────────────────── */

void bot_window_get_active_size(int *w, int *h)
{
    if (g_active_window) {
        if (w) *w = g_active_window->width;
        if (h) *h = g_active_window->height;
    } else {
        if (w) *w = 0;
        if (h) *h = 0;
    }
}

int bot_window_get_title_h(void)
{
    return g_active_window ? g_active_window->title_h : 0;
}

void bot_window_set_should_close(int close)
{
    if (g_active_window) {
        g_active_window->should_close = close;
    }
}

void bot_window_toggle_maximize(void)
{
    if (!g_active_window) return;
    bot_window_t *win = g_active_window;
    
    int sw = 1024, sh = 768;
    bot_window_get_screen_size(&sw, &sh);
    
    if (!win->is_maximized) {
        /* Maximize */
        win->orig_width = win->width;
        win->orig_height = win->height;
        bot_window_resize(win, sw, sh);
        win->is_maximized = 1;
        
        /* Push resize event */
        bot_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = BOT_EVENT_RESIZE;
        ev.resize.width = sw;
        ev.resize.height = sh;
        bot_event_push(&ev);
    } else {
        /* Restore */
        bot_window_resize(win, win->orig_width, win->orig_height);
        win->is_maximized = 0;
        
        /* Push resize event */
        bot_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = BOT_EVENT_RESIZE;
        ev.resize.width = win->orig_width;
        ev.resize.height = win->orig_height;
        bot_event_push(&ev);
    }
}

void bot_window_flip_active(void)
{
    if (g_active_window) {
        bot_window_flip(g_active_window);
    }
}
