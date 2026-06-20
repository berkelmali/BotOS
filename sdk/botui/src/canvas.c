/* ============================================================
 * BotOS Core — Canvas Drawing Engine (Production)
 * ============================================================
 * File:    canvas.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — Pixel-perfect 2D drawing engine.
 *
 * All rendering is performed on a software framebuffer
 * (uint32_t array, ARGB format). The canvas is the lowest
 * drawing primitive — windows and widgets compose on top.
 *
 * Drawing Algorithms:
 *   - Bresenham's line (all octants, integer-only)
 *   - Midpoint circle (8-way symmetry)
 *   - Filled circle (horizontal scanline per octant)
 *   - Fast filled rectangle (memset per row)
 *   - Alpha blending (SRC_OVER compositing)
 *   - Canvas-to-canvas blit (with clip)
 *   - Horizontal/vertical line fast paths
 * ============================================================ */

#include "bot_canvas.h"

#include <stdlib.h>
#include <string.h>

/* ── Canvas Structure ────────────────────────────────────── */

struct bot_canvas {
    int       width;
    int       height;
    int       stride;      /**< Row stride in pixels (== width). */
    uint32_t *pixels;
};

/* ── Internal: Inline Helpers ────────────────────────────── */

static inline int iabs(int v) { return v < 0 ? -v : v; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iclamp(int v, int lo, int hi) { return imax(lo, imin(v, hi)); }

/**
 * Alpha-blend SRC_OVER: blend 'src' on top of 'dst'.
 * Both colors are ARGB (0xAARRGGBB).
 */
static inline uint32_t alpha_blend(uint32_t dst, uint32_t src)
{
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0xFF) return src;      /* Fully opaque — fast path */
    if (sa == 0x00) return dst;      /* Fully transparent */

    uint32_t da = 255 - sa;

    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >>  8) & 0xFF;
    uint32_t sb = (src >>  0) & 0xFF;

    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >>  8) & 0xFF;
    uint32_t db = (dst >>  0) & 0xFF;

    uint32_t or = (sr * sa + dr * da) / 255;
    uint32_t og = (sg * sa + dg * da) / 255;
    uint32_t ob = (sb * sa + db * da) / 255;

    return 0xFF000000 | (or << 16) | (og << 8) | ob;
}

/**
 * Set a pixel with bounds checking and optional alpha blend.
 */
static inline void put_pixel(bot_canvas_t *c, int x, int y, uint32_t color)
{
    if ((unsigned)x < (unsigned)c->width &&
        (unsigned)y < (unsigned)c->height) {

        uint32_t *p = &c->pixels[y * c->stride + x];

        if ((color >> 24) == 0xFF) {
            *p = color;
        } else {
            *p = alpha_blend(*p, color);
        }
    }
}

/**
 * Fast horizontal line (no per-pixel bounds check on x).
 */
static void hline(bot_canvas_t *c, int x1, int x2, int y, uint32_t color)
{
    if (y < 0 || y >= c->height) return;

    int left  = imax(imin(x1, x2), 0);
    int right = imin(imax(x1, x2), c->width - 1);

    uint32_t *row = c->pixels + y * c->stride;

    if ((color >> 24) == 0xFF) {
        /* Opaque — fill directly */
        for (int x = left; x <= right; x++) {
            row[x] = color;
        }
    } else {
        for (int x = left; x <= right; x++) {
            row[x] = alpha_blend(row[x], color);
        }
    }
}

/**
 * Fast vertical line.
 */
static void vline(bot_canvas_t *c, int x, int y1, int y2, uint32_t color)
{
    if (x < 0 || x >= c->width) return;

    int top    = imax(imin(y1, y2), 0);
    int bottom = imin(imax(y1, y2), c->height - 1);

    for (int y = top; y <= bottom; y++) {
        uint32_t *p = &c->pixels[y * c->stride + x];
        if ((color >> 24) == 0xFF) {
            *p = color;
        } else {
            *p = alpha_blend(*p, color);
        }
    }
}

/* ── Public API ──────────────────────────────────────────── */

bot_canvas_t *bot_canvas_create(int width, int height)
{
    if (width <= 0 || height <= 0) return NULL;

    bot_canvas_t *c = (bot_canvas_t *)calloc(1, sizeof(bot_canvas_t));
    if (!c) return NULL;

    c->width  = width;
    c->height = height;
    c->stride = width;

    c->pixels = (uint32_t *)calloc((size_t)(width * height), sizeof(uint32_t));
    if (!c->pixels) {
        free(c);
        return NULL;
    }

    return c;
}

void bot_canvas_destroy(bot_canvas_t *canvas)
{
    if (!canvas) return;
    free(canvas->pixels);
    free(canvas);
}

void bot_canvas_clear(bot_canvas_t *canvas, bot_color_t color)
{
    if (!canvas) return;

    int total = canvas->width * canvas->height;

    /* Optimization: if color is 0x00000000 or all same byte, use memset */
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >>  8);
    uint8_t b = (uint8_t)(color >>  0);
    uint8_t a = (uint8_t)(color >> 24);

    if (a == r && r == g && g == b) {
        memset(canvas->pixels, (int)a, (size_t)total * sizeof(uint32_t));
    } else {
        /* General case: fill word-by-word */
        for (int i = 0; i < total; i++) {
            canvas->pixels[i] = color;
        }
    }
}

void bot_canvas_set_pixel(bot_canvas_t *canvas, int x, int y, bot_color_t color)
{
    if (!canvas) return;
    put_pixel(canvas, x, y, color);
}

void bot_canvas_draw_line(bot_canvas_t *canvas, int x1, int y1,
                          int x2, int y2, bot_color_t color)
{
    if (!canvas) return;

    /* Fast paths for axis-aligned lines */
    if (y1 == y2) { hline(canvas, x1, x2, y1, color); return; }
    if (x1 == x2) { vline(canvas, x1, y1, y2, color); return; }

    /* ── Bresenham's Line Algorithm ──────────────────── */

    int dx = iabs(x2 - x1);
    int dy = -iabs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        put_pixel(canvas, x1, y1, color);

        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void bot_canvas_draw_rect(bot_canvas_t *canvas, int x, int y,
                          int w, int h, bot_color_t color)
{
    if (!canvas || w <= 0 || h <= 0) return;

    /* Four edges using fast h/v line paths */
    hline(canvas, x, x + w - 1, y,         color);  /* Top    */
    hline(canvas, x, x + w - 1, y + h - 1, color);  /* Bottom */
    vline(canvas, x,         y, y + h - 1, color);   /* Left   */
    vline(canvas, x + w - 1, y, y + h - 1, color);   /* Right  */
}

void bot_canvas_fill_rect(bot_canvas_t *canvas, int x, int y,
                          int w, int h, bot_color_t color)
{
    if (!canvas || w <= 0 || h <= 0) return;

    /* Clip to canvas bounds */
    int x1 = imax(x, 0);
    int y1 = imax(y, 0);
    int x2 = imin(x + w, canvas->width);
    int y2 = imin(y + h, canvas->height);

    if (x1 >= x2 || y1 >= y2) return;

    int row_width = x2 - x1;

    if ((color >> 24) == 0xFF) {
        /* Opaque: fast row fill */
        for (int row = y1; row < y2; row++) {
            uint32_t *p = canvas->pixels + row * canvas->stride + x1;

            /* First pixel sets the pattern, then memcpy-replicate */
            p[0] = color;
            if (row_width > 1) {
                /* Fill remaining using word-by-word copy */
                for (int col = 1; col < row_width; col++) {
                    p[col] = color;
                }
            }
        }
    } else {
        /* Semi-transparent: alpha blend each pixel */
        for (int row = y1; row < y2; row++) {
            for (int col = x1; col < x2; col++) {
                put_pixel(canvas, col, row, color);
            }
        }
    }
}

void bot_canvas_draw_circle(bot_canvas_t *canvas, int cx, int cy,
                            int radius, bot_color_t color)
{
    if (!canvas || radius <= 0) return;

    /* ── Midpoint Circle Algorithm (8-way symmetry) ── */

    int x = radius;
    int y = 0;
    int d = 1 - radius;  /* Decision parameter */

    while (x >= y) {
        /* Plot all 8 symmetric points */
        put_pixel(canvas, cx + x, cy + y, color);
        put_pixel(canvas, cx - x, cy + y, color);
        put_pixel(canvas, cx + x, cy - y, color);
        put_pixel(canvas, cx - x, cy - y, color);
        put_pixel(canvas, cx + y, cy + x, color);
        put_pixel(canvas, cx - y, cy + x, color);
        put_pixel(canvas, cx + y, cy - x, color);
        put_pixel(canvas, cx - y, cy - x, color);

        y++;

        if (d <= 0) {
            /* Move East: midpoint is inside circle */
            d += 2 * y + 1;
        } else {
            /* Move South-East: midpoint is outside */
            x--;
            d += 2 * (y - x) + 1;
        }
    }
}

uint32_t *bot_canvas_get_pixels(bot_canvas_t *canvas)
{
    return canvas ? canvas->pixels : NULL;
}

int bot_canvas_width(const bot_canvas_t *canvas)
{
    return canvas ? canvas->width : 0;
}

int bot_canvas_height(const bot_canvas_t *canvas)
{
    return canvas ? canvas->height : 0;
}

void bot_canvas_fill_circle(bot_canvas_t *canvas, int cx, int cy,
                            int radius, bot_color_t color)
{
    if (!canvas || radius <= 0) return;

    int x = radius;
    int y = 0;
    int d = 1 - radius;

    while (x >= y) {
        hline(canvas, cx - x, cx + x, cy + y, color);
        hline(canvas, cx - x, cx + x, cy - y, color);
        hline(canvas, cx - y, cx + y, cy + x, color);
        hline(canvas, cx - y, cx + y, cy - x, color);

        y++;

        if (d <= 0) {
            d += 2 * y + 1;
        } else {
            x--;
            d += 2 * (y - x) + 1;
        }
    }
}
