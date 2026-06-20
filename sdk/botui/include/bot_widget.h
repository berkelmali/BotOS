/* ============================================================
 * BotOS Core — Widget Toolkit API
 * ============================================================
 * File:    bot_widget.h
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Reusable UI widget primitives built on top of bot_canvas.
 * Provides text rendering, buttons, panels, status bars,
 * toolbar items, color swatches, and window frame decoration.
 * ============================================================ */

#ifndef BOTOS_WIDGET_H
#define BOTOS_WIDGET_H

#include "bot_canvas.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Theme ───────────────────────────────────────────────── */

/** Default theme colors (can be overridden before first use). */
typedef struct bot_theme {
    bot_color_t bg;            /**< Window background.          */
    bot_color_t fg;            /**< Primary text.               */
    bot_color_t fg_dim;        /**< Secondary/dimmed text.      */
    bot_color_t accent;        /**< Accent / highlight.         */
    bot_color_t panel;         /**< Panel / sidebar background. */
    bot_color_t panel_border;  /**< Panel / sidebar border.     */
    bot_color_t btn_normal;    /**< Button resting state.       */
    bot_color_t btn_hover;     /**< Button hover state.         */
    bot_color_t btn_active;    /**< Button active/pressed.      */
    bot_color_t status_bg;     /**< Status bar background.      */
    bot_color_t status_fg;     /**< Status bar text.            */
    bot_color_t cursor;        /**< Text cursor color.          */
    bot_color_t highlight;     /**< Current-line highlight.     */
    bot_color_t separator;     /**< Divider lines.              */
} bot_theme_t;

/** Get the current theme (mutable pointer). */
bot_theme_t *bot_theme_get(void);

/* ── Text Rendering (5×7 Bitmap Font) ────────────────────── */

/** Character cell dimensions. */
#define BOT_CHAR_W  6   /* 5px glyph + 1px gap */
#define BOT_CHAR_H  9   /* 7px glyph + 2px pad */

/**
 * Draw a single character at pixel position (px, py).
 * @param scale  Pixel scale factor (1 = 5×7, 2 = 10×14, ...).
 */
void bot_draw_char(bot_canvas_t *c, int px, int py,
                   char ch, bot_color_t color, int scale);

/**
 * Draw a null-terminated string.
 * @return  Pixel width of the rendered string.
 */
int bot_draw_text(bot_canvas_t *c, int px, int py,
                  const char *text, bot_color_t color, int scale);

/**
 * Measure the pixel width of a string without drawing.
 */
int bot_measure_text(const char *text, int scale);

/* ── Buttons ─────────────────────────────────────────────── */

/** Button state flags. */
typedef enum {
    BOT_BTN_NORMAL  = 0,
    BOT_BTN_HOVER   = 1,
    BOT_BTN_ACTIVE  = 2,
} bot_btn_state_t;

/**
 * Draw a rectangular button with centered label.
 */
void bot_draw_button(bot_canvas_t *c, int x, int y, int w, int h,
                     const char *label, bot_btn_state_t state);

/**
 * Hit-test: returns 1 if (mx, my) is inside the button rect.
 */
int bot_button_hit(int btn_x, int btn_y, int btn_w, int btn_h,
                   int mx, int my);

/* ── Panels & Frames ─────────────────────────────────────── */

/**
 * Draw a filled panel (sidebar / toolbar background).
 */
void bot_draw_panel(bot_canvas_t *c, int x, int y, int w, int h);

/**
 * Draw a window frame with title bar.
 * @param title_h  Title bar height (0 to skip).
 */
void bot_draw_window_frame(bot_canvas_t *c, int x, int y, int w, int h,
                           const char *title, int title_h);

/**
 * Draw a horizontal separator line.
 */
void bot_draw_separator(bot_canvas_t *c, int x, int y, int w);

/* ── Status Bar ──────────────────────────────────────────── */

/**
 * Draw a status bar at position (x, y) with given width & height.
 */
void bot_draw_status_bar(bot_canvas_t *c, int x, int y, int w, int h,
                         const char *text);

/* ── Text Box / Input ────────────────────────────────────── */

/**
 * Draw a single-line text input box.
 * @param cursor_pos  Character position of cursor (-1 to hide).
 */
void bot_draw_textbox(bot_canvas_t *c, int x, int y, int w, int h,
                      const char *text, int cursor_pos);

/* ── Color Swatch ────────────────────────────────────────── */

/**
 * Draw a color swatch with optional selection ring.
 */
void bot_draw_color_swatch(bot_canvas_t *c, int x, int y, int size,
                           bot_color_t color, int selected);

/* ── Gradient Fill ───────────────────────────────────────── */

/**
 * Fill a vertical gradient from top_color to bottom_color.
 */
void bot_draw_gradient_v(bot_canvas_t *c, int x, int y, int w, int h,
                         bot_color_t top_color, bot_color_t bottom_color);

/* ── Icon Placeholder ────────────────────────────────────── */

/**
 * Draw a simple icon box with a colored circle and label.
 */
void bot_draw_icon(bot_canvas_t *c, int x, int y, int size,
                   bot_color_t color, const char *label);

/** Load theme from /etc/botos/theme.conf. */
void bot_theme_load(void);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_WIDGET_H */
