/* ============================================================
 * BotOS Core — Canvas Drawing API
 * ============================================================
 * File:    bot_canvas.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_CANVAS_H
#define BOTOS_CANVAS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** RGBA color (0xAARRGGBB). */
typedef uint32_t bot_color_t;

#define BOT_COLOR_BLACK   0xFF000000
#define BOT_COLOR_WHITE   0xFFFFFFFF
#define BOT_COLOR_RED     0xFFFF0000
#define BOT_COLOR_GREEN   0xFF00FF00
#define BOT_COLOR_BLUE    0xFF0000FF
#define BOT_COLOR_CYAN    0xFF00FFFF
#define BOT_COLOR_YELLOW  0xFFFFFF00

#define BOT_RGBA(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/** Opaque canvas handle. */
typedef struct bot_canvas bot_canvas_t;

/** Create a canvas with given dimensions. */
bot_canvas_t *bot_canvas_create(int width, int height);

/** Destroy a canvas. */
void bot_canvas_destroy(bot_canvas_t *canvas);

/** Clear canvas with a color. */
void bot_canvas_clear(bot_canvas_t *canvas, bot_color_t color);

/** Set a single pixel. */
void bot_canvas_set_pixel(bot_canvas_t *canvas, int x, int y, bot_color_t color);

/** Draw a line (Bresenham). */
void bot_canvas_draw_line(bot_canvas_t *canvas, int x1, int y1,
                          int x2, int y2, bot_color_t color);

/** Draw a rectangle outline. */
void bot_canvas_draw_rect(bot_canvas_t *canvas, int x, int y,
                          int w, int h, bot_color_t color);

/** Draw a filled rectangle. */
void bot_canvas_fill_rect(bot_canvas_t *canvas, int x, int y,
                          int w, int h, bot_color_t color);

/** Draw a circle outline. */
void bot_canvas_draw_circle(bot_canvas_t *canvas, int cx, int cy,
                            int radius, bot_color_t color);

/** Draw a filled circle. */
void bot_canvas_fill_circle(bot_canvas_t *canvas, int cx, int cy,
                            int radius, bot_color_t color);

/** Get raw pixel buffer. */
uint32_t *bot_canvas_get_pixels(bot_canvas_t *canvas);

/** Get canvas width. */
int bot_canvas_width(const bot_canvas_t *canvas);

/** Get canvas height. */
int bot_canvas_height(const bot_canvas_t *canvas);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_CANVAS_H */
