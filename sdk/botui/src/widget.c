/* ============================================================
 * BotOS Core — Widget Toolkit Implementation
 * ============================================================
 * File:    widget.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Reusable UI primitives. All apps should draw through these
 * functions instead of issuing raw canvas calls for UI chrome.
 *
 * Contains:
 *   - Global default theme
 *   - Complete 5×7 bitmap font (A-Z, a-z, 0-9, punctuation)
 *   - Button, panel, window frame, status bar, textbox,
 *     color swatch, gradient, and icon drawing functions
 * ============================================================ */

#include "bot_widget.h"
#include "bot_canvas.h"

#include <stdio.h>
#include <string.h>

/* ── Default Theme ───────────────────────────────────────── */

static bot_theme_t g_theme = {
    .bg           = 0xFF181820,  /* Dark charcoal   */
    .fg           = 0xFFD2D7E6,  /* Light lavender  */
    .fg_dim       = 0xFF788298,  /* Muted slate     */
    .accent       = 0xFF648CFF,  /* Soft blue       */
    .panel        = 0xFF1E2233,  /* Sidebar dark    */
    .panel_border = 0xFF505A7A,  /* Subtle border   */
    .btn_normal   = 0xFF32374B,  /* Neutral button  */
    .btn_hover    = 0xFF4B5078,  /* Hover lift      */
    .btn_active   = 0xFF5064B4,  /* Pressed/active  */
    .status_bg    = 0xFF2850A0,  /* Blue status bar */
    .status_fg    = 0xFFF0F5FF,  /* Near-white text */
    .cursor       = 0xFF64B4FF,  /* Bright cursor   */
    .highlight    = 0xFF1E2332,  /* Cur-line bg     */
    .separator    = 0xFF3C4660,  /* Divider lines   */
};

bot_theme_t *bot_theme_get(void)
{
    return &g_theme;
}

/* ── Bitmap Font (5×7) ───────────────────────────────────── */

static const unsigned char g_font[128][7] = {
    /* Digits */
    ['0']={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1']={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2']={0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ['3']={0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    ['4']={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5']={0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ['6']={0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ['7']={0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8']={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9']={0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* Uppercase */
    ['A']={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B']={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C']={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D']={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['E']={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F']={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G']={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    ['H']={0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I']={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J']={0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    ['K']={0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L']={0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M']={0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N']={0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O']={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P']={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['Q']={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['R']={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S']={0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    ['T']={0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U']={0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V']={0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    ['W']={0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    ['X']={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y']={0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z']={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* Lowercase (same glyphs as uppercase for this font) */
    ['a']={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['b']={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['c']={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['d']={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['e']={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['f']={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['g']={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    ['h']={0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['i']={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['j']={0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    ['k']={0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['l']={0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['m']={0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['n']={0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['o']={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['p']={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['q']={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['r']={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['s']={0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    ['t']={0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['u']={0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['v']={0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    ['w']={0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    ['x']={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['y']={0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['z']={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* Punctuation & symbols */
    [' ']={0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [':']={0x00,0x04,0x04,0x00,0x04,0x04,0x00},
    ['.']={0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    [',']={0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    ['-']={0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    ['+']={0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    ['*']={0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00},
    ['/']={0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    ['!']={0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    ['?']={0x0E,0x11,0x01,0x06,0x04,0x00,0x04},
    ['(']={0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    [')']={0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    ['[']={0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    [']']={0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
    ['{']={0x06,0x08,0x08,0x10,0x08,0x08,0x06},
    ['}']={0x0C,0x02,0x02,0x01,0x02,0x02,0x0C},
    ['<']={0x01,0x02,0x04,0x08,0x04,0x02,0x01},
    ['>']={0x10,0x08,0x04,0x02,0x04,0x08,0x10},
    ['=']={0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    ['"']={0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
    ['\'']={0x04,0x04,0x00,0x00,0x00,0x00,0x00},
    [';']={0x00,0x04,0x04,0x00,0x04,0x04,0x08},
    ['#']={0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00},
    ['_']={0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
    ['\\']={0x10,0x08,0x08,0x04,0x02,0x02,0x01},
    ['@']={0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
    ['$']={0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
    ['%']={0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    ['&']={0x0C,0x12,0x0C,0x0D,0x12,0x12,0x0D},
    ['~']={0x00,0x00,0x0D,0x12,0x00,0x00,0x00},
    ['^']={0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
    ['|']={0x04,0x04,0x04,0x04,0x04,0x04,0x04},
};

/* ── Text Rendering ──────────────────────────────────────── */

#ifdef BOTOS_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>

/* Real, anti-aliased TTF glyph rendering, used automatically in place
 * of the bitmap font above when a FreeType-capable build can actually
 * load the bundled font. Every layout in this codebase (button
 * heights, line spacing, icon label placement, ...) was written
 * around the bitmap font's fixed BOT_CHAR_W x BOT_CHAR_H cell, so
 * this deliberately keeps drawing into that exact same cell — same
 * (px, py, scale) contract, same bot_measure_text() advance — rather
 * than switching to the font's own (proportional, differently sized)
 * metrics. Only the ink inside each cell gets nicer; nothing that
 * currently depends on fixed-width text layout has to change.
 *
 * If the font file can't be found or FreeType fails to initialize,
 * ft_try_init() returns 0 once and every caller falls straight back
 * to the bitmap glyphs below — there's no hard dependency either way.
 */

#define FT_GLYPH_CACHE_SIZE 512

typedef struct {
    int used;
    int ch;
    int pixel_size;
    int width, height;
    int left, top;
    unsigned char *bitmap; /* width*height bytes, 8-bit alpha coverage */
} ft_glyph_cache_t;

static FT_Library        ft_library;
static FT_Face            ft_face;
static int                ft_state = 0;  /* 0=untried, 1=ready, -1=unavailable */
static ft_glyph_cache_t   ft_cache[FT_GLYPH_CACHE_SIZE];
static int                ft_cache_count = 0;

static int ft_try_init(void)
{
    if (ft_state != 0) return ft_state == 1;

    const char *candidates[] = {
        "/usr/share/botos/fonts/DejaVuSansMono.ttf",
#ifdef BOTOS_FONT_DEV_PATH
        BOTOS_FONT_DEV_PATH,
#endif
        NULL
    };

    if (FT_Init_FreeType(&ft_library) != 0) {
        ft_state = -1;
        return 0;
    }

    for (int i = 0; candidates[i]; i++) {
        if (FT_New_Face(ft_library, candidates[i], 0, &ft_face) == 0) {
            ft_state = 1;
            return 1;
        }
    }

    FT_Done_FreeType(ft_library);
    ft_state = -1;
    return 0;
}

static ft_glyph_cache_t *ft_get_glyph(int ch, int pixel_size)
{
    for (int i = 0; i < ft_cache_count; i++) {
        if (ft_cache[i].used && ft_cache[i].ch == ch &&
            ft_cache[i].pixel_size == pixel_size) {
            return &ft_cache[i];
        }
    }
    if (ft_cache_count >= FT_GLYPH_CACHE_SIZE) {
        /* Cache full — extremely unlikely given ASCII x a handful of
         * scale factors, but stop growing rather than overflow it;
         * already-cached glyphs keep rendering correctly. */
        return NULL;
    }

    FT_Set_Pixel_Sizes(ft_face, 0, (unsigned)pixel_size);
    if (FT_Load_Char(ft_face, (unsigned long)ch, FT_LOAD_RENDER) != 0) {
        return NULL;
    }

    FT_GlyphSlot slot = ft_face->glyph;
    ft_glyph_cache_t *entry = &ft_cache[ft_cache_count];
    entry->ch         = ch;
    entry->pixel_size = pixel_size;
    entry->width      = (int)slot->bitmap.width;
    entry->height     = (int)slot->bitmap.rows;
    entry->left       = slot->bitmap_left;
    entry->top        = slot->bitmap_top;
    entry->bitmap     = NULL;

    size_t n = (size_t)entry->width * (size_t)entry->height;
    if (n > 0) {
        entry->bitmap = malloc(n);
        if (entry->bitmap) memcpy(entry->bitmap, slot->bitmap.buffer, n);
    }
    entry->used = 1;
    ft_cache_count++;
    return entry;
}

static void ft_blend_pixel(bot_canvas_t *c, int x, int y,
                           bot_color_t fg, unsigned char alpha)
{
    if (alpha == 0) return;
    int cw = bot_canvas_width(c), ch = bot_canvas_height(c);
    if (x < 0 || y < 0 || x >= cw || y >= ch) return;

    uint32_t *px = bot_canvas_get_pixels(c);
    if (!px) return;

    uint32_t bg = px[(size_t)y * (size_t)cw + (size_t)x];
    int bg_r = (int)((bg >> 16) & 0xFF), bg_g = (int)((bg >> 8) & 0xFF), bg_b = (int)(bg & 0xFF);
    int fg_r = (int)((fg >> 16) & 0xFF), fg_g = (int)((fg >> 8) & 0xFF), fg_b = (int)(fg & 0xFF);

    int out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    int out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    int out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    px[(size_t)y * (size_t)cw + (size_t)x] =
        0xFF000000u | ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | (uint32_t)out_b;
}

/* Returns 1 if it drew the glyph (caller should not fall back), 0 if
 * FreeType isn't usable or this character has no renderable glyph. */
static int ft_draw_char(bot_canvas_t *c, int px, int py,
                        char ch, bot_color_t color, int scale)
{
    if (!ft_try_init()) return 0;
    unsigned char idx = (unsigned char)ch;
    if (idx >= 128) return 0;

    /* ~8px em at scale 1 lands the glyph ink in roughly the same
     * footprint the 5x7 bitmap font occupied. */
    int pixel_size = 8 * (scale <= 1 ? 1 : scale);
    ft_glyph_cache_t *g = ft_get_glyph(idx, pixel_size);
    if (!g) return 0;
    if (!g->bitmap) return 1; /* space or similarly ink-free glyph: nothing to draw, handled */

    int baseline_y = py + (BOT_CHAR_H - 2) * (scale <= 1 ? 1 : scale);
    for (int gy = 0; gy < g->height; gy++) {
        for (int gx = 0; gx < g->width; gx++) {
            unsigned char a = g->bitmap[(size_t)gy * (size_t)g->width + (size_t)gx];
            ft_blend_pixel(c, px + g->left + gx, baseline_y - g->top + gy, color, a);
        }
    }
    return 1;
}
#endif /* BOTOS_HAS_FREETYPE */

void bot_draw_char(bot_canvas_t *c, int px, int py,
                   char ch, bot_color_t color, int scale)
{
#ifdef BOTOS_HAS_FREETYPE
    if (ft_draw_char(c, px, py, ch, color, scale)) return;
#endif

    unsigned char idx = (unsigned char)ch;
    if (idx >= 128) return;

    const unsigned char *glyph = g_font[idx];

    for (int row = 0; row < 7; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                if (scale <= 1) {
                    bot_canvas_set_pixel(c, px + col, py + row, color);
                } else {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            bot_canvas_set_pixel(c,
                                px + col * scale + sx,
                                py + row * scale + sy, color);
                }
            }
        }
    }
}

int bot_draw_text(bot_canvas_t *c, int px, int py,
                  const char *text, bot_color_t color, int scale)
{
    if (!text) return 0;
    int x = px;
    int cw = (scale <= 1) ? BOT_CHAR_W : 6 * scale;
    for (int i = 0; text[i]; i++) {
        bot_draw_char(c, x, py, text[i], color, scale);
        x += cw;
    }
    return x - px;
}

int bot_measure_text(const char *text, int scale)
{
    if (!text) return 0;
    int cw = (scale <= 1) ? BOT_CHAR_W : 6 * scale;
    return (int)strlen(text) * cw;
}

/* ── Buttons ─────────────────────────────────────────────── */

void bot_draw_button(bot_canvas_t *c, int x, int y, int w, int h,
                     const char *label, bot_btn_state_t state)
{
    bot_color_t bg;
    switch (state) {
        case BOT_BTN_HOVER:  bg = g_theme.btn_hover;  break;
        case BOT_BTN_ACTIVE: bg = g_theme.btn_active;  break;
        default:             bg = g_theme.btn_normal;  break;
    }

    bot_canvas_fill_rect(c, x, y, w, h, bg);

    if (label) {
        int tw = bot_measure_text(label, 1);
        int tx = x + (w - tw) / 2;
        int ty = y + (h - 7) / 2;
        bot_draw_text(c, tx, ty, label, g_theme.fg, 1);
    }
}

int bot_button_hit(int bx, int by, int bw, int bh, int mx, int my)
{
    return (mx >= bx && mx < bx + bw && my >= by && my < by + bh);
}

/* ── Panels & Frames ─────────────────────────────────────── */

void bot_draw_panel(bot_canvas_t *c, int x, int y, int w, int h)
{
    bot_canvas_fill_rect(c, x, y, w, h, g_theme.panel);
}

void bot_draw_window_frame(bot_canvas_t *c, int x, int y, int w, int h,
                           const char *title, int title_h)
{
    /* Background */
    bot_canvas_fill_rect(c, x, y, w, h, g_theme.bg);

    /* Border */
    bot_canvas_draw_rect(c, x, y, w, h, g_theme.panel_border);

    /* Title bar */
    if (title_h > 0) {
        bot_canvas_fill_rect(c, x, y, w, title_h, g_theme.panel);
        bot_draw_separator(c, x, y + title_h, w);

        if (title) {
            bot_draw_text(c, x + 8, y + (title_h - 7) / 2,
                          title, g_theme.fg, 1);
        }

        /* Draw window controls (macOS style colored circles) */
        bot_color_t col_close = BOT_RGBA(255, 95, 87, 255);   /* Soft Red */
        bot_color_t col_min   = BOT_RGBA(254, 188, 46, 255);  /* Soft Yellow */
        bot_color_t col_max   = BOT_RGBA(40, 200, 64, 255);   /* Soft Green */

        int r = 5;
        int cy = y + title_h / 2;

        /* Close button (Red) */
        bot_canvas_fill_circle(c, x + w - 15, cy, r, col_close);
        /* Maximize button (Green) */
        bot_canvas_fill_circle(c, x + w - 33, cy, r, col_max);
        /* Minimize button (Yellow) */
        bot_canvas_fill_circle(c, x + w - 51, cy, r, col_min);
    }
}

void bot_draw_separator(bot_canvas_t *c, int x, int y, int w)
{
    for (int i = 0; i < w; i++) {
        bot_canvas_set_pixel(c, x + i, y, g_theme.separator);
    }
}

/* ── Status Bar ──────────────────────────────────────────── */

void bot_draw_status_bar(bot_canvas_t *c, int x, int y, int w, int h,
                         const char *text)
{
    bot_canvas_fill_rect(c, x, y, w, h, g_theme.status_bg);

    if (text) {
        bot_draw_text(c, x + 4, y + (h - 7) / 2, text, g_theme.status_fg, 1);
    }
}

/* ── Text Box ────────────────────────────────────────────── */

void bot_draw_textbox(bot_canvas_t *c, int x, int y, int w, int h,
                      const char *text, int cursor_pos)
{
    /* Background */
    bot_canvas_fill_rect(c, x, y, w, h, g_theme.bg);
    bot_canvas_draw_rect(c, x, y, w, h, g_theme.panel_border);

    /* Text */
    if (text) {
        int tx = x + 4;
        int ty = y + (h - 7) / 2;
        bot_draw_text(c, tx, ty, text, g_theme.fg, 1);

        /* Cursor */
        if (cursor_pos >= 0) {
            int cx = tx + cursor_pos * BOT_CHAR_W;
            bot_canvas_fill_rect(c, cx, y + 2, 2, h - 4, g_theme.cursor);
        }
    }
}

/* ── Color Swatch ────────────────────────────────────────── */

void bot_draw_color_swatch(bot_canvas_t *c, int x, int y, int size,
                           bot_color_t color, int selected)
{
    bot_canvas_fill_rect(c, x, y, size, size, color);

    if (selected) {
        bot_canvas_draw_rect(c, x - 1, y - 1, size + 2, size + 2,
                             BOT_COLOR_WHITE);
        bot_canvas_draw_rect(c, x - 2, y - 2, size + 4, size + 4,
                             BOT_COLOR_WHITE);
    }
}

/* ── Gradient ────────────────────────────────────────────── */

void bot_draw_gradient_v(bot_canvas_t *c, int x, int y, int w, int h,
                         bot_color_t top_color, bot_color_t bottom_color)
{
    int tr = (top_color >> 16) & 0xFF, tg = (top_color >> 8) & 0xFF;
    int tb = (top_color >>  0) & 0xFF;
    int br = (bottom_color >> 16) & 0xFF, bg_ = (bottom_color >> 8) & 0xFF;
    int bb = (bottom_color >>  0) & 0xFF;

    for (int row = 0; row < h; row++) {
        float t = (h > 1) ? (float)row / (float)(h - 1) : 0.0f;
        int r = (int)(tr + t * (br - tr));
        int g = (int)(tg + t * (bg_ - tg));
        int b = (int)(tb + t * (bb - tb));
        bot_color_t col = BOT_RGBA(r, g, b, 255);

        for (int col_x = x; col_x < x + w; col_x++) {
            bot_canvas_set_pixel(c, col_x, y + row, col);
        }
    }
}

/* ── Icon ────────────────────────────────────────────────── */

void bot_draw_icon(bot_canvas_t *c, int x, int y, int size,
                   bot_color_t color, const char *label)
{
    bot_canvas_fill_rect(c, x, y, size, size, g_theme.panel);
    bot_canvas_draw_rect(c, x, y, size, size, g_theme.panel_border);

    /* Colored circle */
    int cx = x + size / 2;
    int cy = y + size / 2;
    bot_canvas_draw_circle(c, cx, cy, size / 3, color);
    bot_canvas_draw_circle(c, cx, cy, size / 3 - 4, color);

    /* Label below */
    if (label) {
        bot_draw_text(c, x + 2, y + size + 4, label, g_theme.fg_dim, 1);
    }
}

void bot_theme_load(void)
{
    FILE *f = fopen("/etc/botos/theme.conf", "r");
    if (!f) return;

    char line[128];
    char theme_name[32] = "dark";

    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            val[strcspn(val, "\r\n")] = '\0';
            if (strcmp(key, "theme") == 0) {
                strncpy(theme_name, val, sizeof(theme_name) - 1);
            }
        }
    }
    fclose(f);

    bot_theme_t *t = bot_theme_get();
    if (strcmp(theme_name, "light") == 0) {
        t->bg           = 0xFFF0F0F0;
        t->fg           = 0xFF181820;
        t->fg_dim       = 0xFF606060;
        t->accent       = 0xFF3264FF;
        t->panel        = 0xFFE0E0E0;
        t->panel_border = 0xFFB0B0B0;
        t->btn_normal   = 0xFFD0D0D0;
        t->btn_hover    = 0xFFC0C0C0;
        t->btn_active   = 0xFFB0B0B0;
        t->status_bg    = 0xFF3264FF;
        t->status_fg    = 0xFFFFFFFF;
        t->cursor       = 0xFF3264FF;
        t->highlight    = 0xFFE8E8E8;
        t->separator    = 0xFFD0D0D0;
    } else if (strcmp(theme_name, "matrix") == 0) {
        t->bg           = 0xFF000000;
        t->fg           = 0xFF00FF00;
        t->fg_dim       = 0xFF008800;
        t->accent       = 0xFF00AA00;
        t->panel        = 0xFF051005;
        t->panel_border = 0xFF005500;
        t->btn_normal   = 0xFF0A200A;
        t->btn_hover    = 0xFF0F300F;
        t->btn_active   = 0xFF154515;
        t->status_bg    = 0xFF003300;
        t->status_fg    = 0xFF00FF00;
        t->cursor       = 0xFF00FF00;
        t->highlight    = 0xFF0A150A;
        t->separator    = 0xFF004400;
    } else if (strcmp(theme_name, "cyberpunk") == 0) {
        t->bg           = 0xFF0D0221;
        t->fg           = 0xFF00F0FF;
        t->fg_dim       = 0xFF7A5C99;
        t->accent       = 0xFFFF2E9A;
        t->panel        = 0xFF1A0B33;
        t->panel_border = 0xFF7A2E8C;
        t->btn_normal   = 0xFF2B1249;
        t->btn_hover    = 0xFF3D1A66;
        t->btn_active   = 0xFF5C1F7A;
        t->status_bg    = 0xFF3D0066;
        t->status_fg    = 0xFF00F0FF;
        t->cursor       = 0xFFFF2E9A;
        t->highlight    = 0xFF241040;
        t->separator    = 0xFF4A2266;
    } else if (strcmp(theme_name, "forest") == 0) {
        t->bg           = 0xFF16241A;
        t->fg           = 0xFFDCE8D5;
        t->fg_dim       = 0xFF7A9070;
        t->accent       = 0xFFC9A227;
        t->panel        = 0xFF1E2E22;
        t->panel_border = 0xFF4A6B45;
        t->btn_normal   = 0xFF2A3D2A;
        t->btn_hover    = 0xFF375035;
        t->btn_active   = 0xFF4A6B3A;
        t->status_bg    = 0xFF2D4A2A;
        t->status_fg    = 0xFFF0EAD6;
        t->cursor       = 0xFFC9A227;
        t->highlight    = 0xFF223322;
        t->separator    = 0xFF3A5236;
    } else if (strcmp(theme_name, "ocean") == 0) {
        t->bg           = 0xFF0A1929;
        t->fg           = 0xFFD5EEF5;
        t->fg_dim       = 0xFF6B8FA3;
        t->accent       = 0xFF1FC8C8;
        t->panel        = 0xFF0F2438;
        t->panel_border = 0xFF2E5C77;
        t->btn_normal   = 0xFF1A3550;
        t->btn_hover    = 0xFF234563;
        t->btn_active   = 0xFF1F6B7A;
        t->status_bg    = 0xFF0F4C5C;
        t->status_fg    = 0xFFE0F7FA;
        t->cursor       = 0xFF1FC8C8;
        t->highlight    = 0xFF15304A;
        t->separator    = 0xFF1F4560;
    } else {
        /* Default Dark */
        t->bg           = 0xFF181820;
        t->fg           = 0xFFD2D7E6;
        t->fg_dim       = 0xFF788298;
        t->accent       = 0xFF648CFF;
        t->panel        = 0xFF1E2233;
        t->panel_border = 0xFF505A7A;
        t->btn_normal   = 0xFF32374B;
        t->btn_hover    = 0xFF4B5078;
        t->btn_active   = 0xFF5064B4;
        t->status_bg    = 0xFF2850A0;
        t->status_fg    = 0xFFF0F5FF;
        t->cursor       = 0xFF64B4FF;
        t->highlight    = 0xFF1E2332;
        t->separator    = 0xFF3C4660;
    }
}
