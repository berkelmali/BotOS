/* ============================================================
 * BotOS Core — BotPaint (Production)
 * ============================================================
 * File:    botpaint.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — Production paint application.
 * Refactored to use the bot_widget toolkit for toolbar,
 * buttons, color swatches, and text rendering.
 *
 * Layout (800×600):
 *   ┌──────┬───────────────────────────────────┐
 *   │Tools │          Drawing Canvas           │
 *   │ Pen  │                                   │
 *   │ Line │                                   │
 *   │ Rect │                                   │
 *   │ Circ │                                   │
 *   │ Fill │                                   │
 *   │Color │                                   │
 *   │ ■■■  │                                   │
 *   └──────┴───────────────────────────────────┘
 * ============================================================ */

#include "bot_ui.h"
#include "bot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Layout ──────────────────────────────────────────────── */

#define TITLE_H          24
#define TOOLBAR_WIDTH    64
#define CANVAS_X         TOOLBAR_WIDTH
#define TOOL_BTN_H       24
#define TOOL_BTN_W       (TOOLBAR_WIDTH - 8)
#define TOOL_BTN_GAP     28
#define SWATCH_SIZE      22

static int g_width = 800;
static int g_height = 600;

/* ── Tool Types ──────────────────────────────────────────── */

typedef enum {
    TOOL_PEN = 0, TOOL_LINE, TOOL_RECT, TOOL_CIRCLE, TOOL_FILL, TOOL_COUNT
} tool_t;

static const char *g_tool_labels[] = { "PEN", "LINE", "RECT", "CIRC", "FILL" };

/* ── Color Palette ───────────────────────────────────────── */

#define PALETTE_COUNT  10

static const bot_color_t g_palette[PALETTE_COUNT] = {
    BOT_RGBA(255,255,255,255), BOT_RGBA(255, 60, 60,255),
    BOT_RGBA(255,160, 40,255), BOT_RGBA(255,240, 60,255),
    BOT_RGBA( 60,220, 60,255), BOT_RGBA( 60,180,255,255),
    BOT_RGBA(160, 80,255,255), BOT_RGBA(255,120,180,255),
    BOT_RGBA(140,140,140,255), BOT_RGBA(  0,  0,  0,255),
};

/* ── State ───────────────────────────────────────────────── */

static bot_window_t *g_window     = NULL;
static bot_canvas_t *g_canvas     = NULL;  /* Drawing surface  */
static bot_canvas_t *g_overlay    = NULL;  /* Composited frame */
static tool_t        g_tool       = TOOL_PEN;
static int           g_color_idx  = 0;
static bot_color_t   g_brush      = BOT_RGBA(255,255,255,255);
static int           g_brush_size = 2;
static int           g_drawing    = 0;
static int           g_drag_x, g_drag_y, g_last_x, g_last_y;
static int           g_needs_redraw = 1;
static uint32_t     *g_undo_buf    = NULL;

/* ── Dialog State ────────────────────────────────────────── */
static int           g_show_dialog = 0; /* 0: none, 1: open, 2: save */
static char          g_dialog_filename[128] = "";
static int           g_dialog_cursor = 0;

static int botpaint_save(const char *filename)
{
    char resolved[256];
    char with_ext[128];
    
    // Check if filename has an extension, if not, append .botimg
    const char *ext = strrchr(filename, '.');
    if (!ext) {
        snprintf(with_ext, sizeof(with_ext), "%s.botimg", filename);
        filename = with_ext;
    }

    if (filename[0] != '/') {
        snprintf(resolved, sizeof(resolved), "/root/%s", filename);
    } else {
        strncpy(resolved, filename, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    FILE *f = fopen(resolved, "w");
    if (!f) return -1;
    
    uint32_t w = (uint32_t)bot_canvas_width(g_canvas);
    uint32_t h = (uint32_t)bot_canvas_height(g_canvas);
    
    fprintf(f, "BOTIMG_ASCII\n%u %u\n", w, h);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    for (uint32_t i = 0; i < w * h; i++) {
        fprintf(f, "%08X ", px[i]);
        if ((i + 1) % w == 0) {
            fprintf(f, "\n");
        }
    }
    
    fclose(f);

    /* Update dialog filename with the resolved absolute path */
    strncpy(g_dialog_filename, resolved, sizeof(g_dialog_filename) - 1);
    g_dialog_filename[sizeof(g_dialog_filename) - 1] = '\0';
    g_dialog_cursor = (int)strlen(g_dialog_filename);

    return 0;
}

static int botpaint_load(const char *filename)
{
    char resolved[256];
    if (filename[0] != '/') {
        snprintf(resolved, sizeof(resolved), "/root/%s", filename);
    } else {
        strncpy(resolved, filename, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    FILE *f = fopen(resolved, "r");
    if (!f) return -1;
    
    char header[32];
    int is_ascii = 0;
    uint32_t w = 0, h = 0;
    
    if (fgets(header, sizeof(header), f)) {
        header[strcspn(header, "\r\n")] = '\0';
        if (strcmp(header, "BOTIMG_ASCII") == 0) {
            is_ascii = 1;
            if (fscanf(f, "%u %u", &w, &h) != 2) {
                fclose(f);
                return -1;
            }
        } else {
            /* Try binary */
            fseek(f, 0, SEEK_SET);
            uint32_t magic = 0;
            if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x474D4942) {
                fclose(f);
                return -1;
            }
            if (fread(&w, sizeof(w), 1, f) != 1 || fread(&h, sizeof(h), 1, f) != 1) {
                fclose(f);
                return -1;
            }
        }
    } else {
        fclose(f);
        return -1;
    }
    
    if (!g_canvas || bot_canvas_width(g_canvas) != (int)w || bot_canvas_height(g_canvas) != (int)h) {
        bot_canvas_destroy(g_canvas);
        g_canvas = bot_canvas_create((int)w, (int)h);
        if (!g_canvas) {
            fclose(f);
            return -1;
        }
        
        int title_h = TITLE_H;
        int win_w = (int)w + TOOLBAR_WIDTH;
        int win_h = (int)h + title_h;
        if (g_window) {
            bot_window_resize(g_window, win_w, win_h);
        }
        
        bot_canvas_destroy(g_overlay);
        g_overlay = bot_canvas_create(win_w, win_h);
        
        g_width = win_w;
        g_height = win_h;
    }
    
    if (is_ascii) {
        uint32_t *px = bot_canvas_get_pixels(g_canvas);
        for (uint32_t i = 0; i < w * h; i++) {
            unsigned int val = 0;
            if (fscanf(f, "%X", &val) != 1) {
                val = 0xFF000000;
            }
            px[i] = (uint32_t)val;
        }
    } else {
        fread(bot_canvas_get_pixels(g_canvas), sizeof(uint32_t), w * h, f);
    }
    
    fclose(f);
    
    free(g_undo_buf);
    g_undo_buf = NULL;
    
    /* Update dialog filename with the resolved absolute path */
    strncpy(g_dialog_filename, resolved, sizeof(g_dialog_filename) - 1);
    g_dialog_filename[sizeof(g_dialog_filename) - 1] = '\0';
    g_dialog_cursor = (int)strlen(g_dialog_filename);

    g_needs_redraw = 1;
    return 0;
}

static void on_dialog_mouse_down(int mx, int my)
{
    int dw = 320;
    int dh = 120;
    int dx = (g_width - dw) / 2;
    int dy = (g_height - dh) / 2;

    /* OK button click */
    if (bot_button_hit(dx + 16, dy + 85, 130, 24, mx, my)) {
        if (strlen(g_dialog_filename) > 0) {
            if (g_show_dialog == 1) {
                botpaint_load(g_dialog_filename);
            } else if (g_show_dialog == 2) {
                botpaint_save(g_dialog_filename);
            }
        }
        g_show_dialog = 0;
        g_needs_redraw = 1;
    }
    /* Cancel button click */
    else if (bot_button_hit(dx + dw - 146, dy + 85, 130, 24, mx, my)) {
        g_show_dialog = 0;
        g_needs_redraw = 1;
    }
}

static void on_dialog_key_down(int key, int mods)
{
    (void)mods;
    if (key == BOT_KEY_ESCAPE) {
        g_show_dialog = 0;
        g_needs_redraw = 1;
        return;
    }
    if (key == BOT_KEY_ENTER || key == '\n' || key == '\r') {
        if (strlen(g_dialog_filename) > 0) {
            if (g_show_dialog == 1) {
                botpaint_load(g_dialog_filename);
            } else if (g_show_dialog == 2) {
                botpaint_save(g_dialog_filename);
            }
        }
        g_show_dialog = 0;
        g_needs_redraw = 1;
        return;
    }
    if (key == BOT_KEY_BACKSPACE || key == 127) {
        int len = (int)strlen(g_dialog_filename);
        if (len > 0) {
            g_dialog_filename[len - 1] = '\0';
            g_dialog_cursor = len - 1;
            g_needs_redraw = 1;
        }
        return;
    }
    
    if (key >= 32 && key <= 126) {
        int len = (int)strlen(g_dialog_filename);
        if (len < (int)sizeof(g_dialog_filename) - 1) {
            g_dialog_filename[len] = (char)key;
            g_dialog_filename[len + 1] = '\0';
            g_dialog_cursor = len + 1;
            g_needs_redraw = 1;
        }
    }
}

static void render_dialog(bot_canvas_t *ov)
{
    uint32_t *px = bot_canvas_get_pixels(ov);
    if (px) {
        for (int i = 0; i < g_width * g_height; i++) {
            uint32_t c = px[i];
            uint8_t a = (c >> 24) & 0xFF;
            uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * 0.4f);
            uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * 0.4f);
            uint8_t b = (uint8_t)(((c >> 0) & 0xFF) * 0.4f);
            px[i] = BOT_RGBA(r, g, b, a);
        }
    }

    int dw = 320;
    int dh = 120;
    int dx = (g_width - dw) / 2;
    int dy = (g_height - dh) / 2;

    bot_theme_t *th = bot_theme_get();
    bot_draw_window_frame(ov, dx, dy, dw, dh, 
                          g_show_dialog == 1 ? "Open Image" : "Save Image", 20);

    bot_draw_text(ov, dx + 16, dy + 32, "Enter filename (e.g. /root/art.botimg):", th->fg, 1);
    bot_draw_textbox(ov, dx + 16, dy + 50, dw - 32, 24, g_dialog_filename, g_dialog_cursor);
    bot_draw_button(ov, dx + 16, dy + 85, 130, 24, "OK", BOT_BTN_NORMAL);
    bot_draw_button(ov, dx + dw - 146, dy + 85, 130, 24, "Cancel", BOT_BTN_NORMAL);
}

static void save_undo(void)
{
    int canvas_w = g_width - TOOLBAR_WIDTH;
    int canvas_h = g_height - TITLE_H;
    if (!g_undo_buf)
        g_undo_buf = (uint32_t *)malloc(
            (size_t)(canvas_w * canvas_h) * sizeof(uint32_t));
    if (g_undo_buf) {
        memcpy(g_undo_buf, bot_canvas_get_pixels(g_canvas),
               (size_t)(canvas_w * canvas_h) * sizeof(uint32_t));
    }
}

static void do_undo(void)
{
    if (!g_undo_buf) return;
    int canvas_w = g_width - TOOLBAR_WIDTH;
    int canvas_h = g_height - TITLE_H;
    memcpy(bot_canvas_get_pixels(g_canvas), g_undo_buf,
           (size_t)(canvas_w * canvas_h) * sizeof(uint32_t));
    g_needs_redraw = 1;
}

/* ── Thick Pixel ─────────────────────────────────────────── */

static void thick_pixel(bot_canvas_t *c, int x, int y,
                        bot_color_t color, int size)
{
    if (size <= 1) { bot_canvas_set_pixel(c, x, y, color); return; }
    int half = size / 2;
    bot_canvas_fill_rect(c, x - half, y - half, size, size, color);
}

/* ── Flood Fill ──────────────────────────────────────────── */

static void flood_fill(bot_canvas_t *c, int sx, int sy, bot_color_t fill)
{
    int w = bot_canvas_width(c), h = bot_canvas_height(c);
    uint32_t *px = bot_canvas_get_pixels(c);
    if (sx < 0 || sx >= w || sy < 0 || sy >= h) return;

    uint32_t target = px[sy * w + sx];
    if (target == fill) return;

    typedef struct { int x, y; } pt_t;
    int cap = 4096;
    pt_t *stack = (pt_t *)malloc((size_t)cap * sizeof(pt_t));
    if (!stack) return;
    int top = 0;
    stack[top++] = (pt_t){sx, sy};

    while (top > 0) {
        pt_t p = stack[--top];
        if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h) continue;
        if (px[p.y * w + p.x] != target) continue;

        int left = p.x, right = p.x;
        while (left > 0 && px[p.y * w + (left-1)] == target) left--;
        while (right < w-1 && px[p.y * w + (right+1)] == target) right++;

        for (int i = left; i <= right; i++) px[p.y * w + i] = fill;

        for (int i = left; i <= right; i++) {
            if (top + 2 >= cap) {
                cap *= 2;
                pt_t *ns = (pt_t *)realloc(stack, (size_t)cap * sizeof(pt_t));
                if (!ns) break;
                stack = ns;
            }
            if (p.y > 0   && px[(p.y-1)*w+i] == target) stack[top++] = (pt_t){i, p.y-1};
            if (p.y < h-1 && px[(p.y+1)*w+i] == target) stack[top++] = (pt_t){i, p.y+1};
        }
    }
    free(stack);
}

/* ── Toolbar Rendering (Widget-Based) ────────────────────── */

static void render_toolbar(bot_canvas_t *ov)
{
    bot_draw_panel(ov, 0, TITLE_H, TOOLBAR_WIDTH, g_height - TITLE_H);

    /* Tool buttons */
    for (int i = 0; i < TOOL_COUNT; i++) {
        int by = TITLE_H + 4 + i * TOOL_BTN_GAP;
        bot_btn_state_t st = (g_tool == (tool_t)i) ? BOT_BTN_ACTIVE : BOT_BTN_NORMAL;
        bot_draw_button(ov, 4, by, TOOL_BTN_W, TOOL_BTN_H, g_tool_labels[i], st);
    }

    /* OPEN and SAVE buttons */
    int open_y = TITLE_H + 4 + 5 * TOOL_BTN_GAP;
    int save_y = TITLE_H + 4 + 6 * TOOL_BTN_GAP;
    bot_draw_button(ov, 4, open_y, TOOL_BTN_W, TOOL_BTN_H, "OPEN", BOT_BTN_NORMAL);
    bot_draw_button(ov, 4, save_y, TOOL_BTN_W, TOOL_BTN_H, "SAVE", BOT_BTN_NORMAL);

    /* Separator */
    bot_draw_separator(ov, 4, TITLE_H + 4 + 7 * TOOL_BTN_GAP, TOOL_BTN_W);

    /* Color swatches */
    int palette_y = TITLE_H + 12 + 7 * TOOL_BTN_GAP;
    for (int i = 0; i < PALETTE_COUNT; i++) {
        int row = i / 2, col = i % 2;
        int px = 8 + col * 26, py = palette_y + row * 26;
        bot_draw_color_swatch(ov, px, py, SWATCH_SIZE,
                              g_palette[i], i == g_color_idx);
    }

    /* Brush size label and preview */
    int sz_y = palette_y + 5 * 26 + 8;
    bot_theme_t *th = bot_theme_get();
    bot_draw_text(ov, 8, sz_y, "SIZE", th->fg, 1);
    bot_canvas_draw_circle(ov, TOOLBAR_WIDTH / 2, sz_y + 20,
                           g_brush_size, g_brush);
}

/* ── Full Composite ──────────────────────────────────────── */

static void render_frame(void)
{
    bot_canvas_clear(g_overlay, BOT_RGBA(20, 22, 30, 255));
    
    /* Draw window frame */
    bot_draw_window_frame(g_overlay, 0, 0, g_width, g_height, "Paint", TITLE_H);

    render_toolbar(g_overlay);

    /* Blit drawing canvas into overlay */
    uint32_t *opx = bot_canvas_get_pixels(g_overlay);
    uint32_t *cpx = bot_canvas_get_pixels(g_canvas);
    int canvas_w = g_width - TOOLBAR_WIDTH;
    int canvas_h = g_height - TITLE_H;
    
    if (opx && cpx) {
        for (int y = 0; y < canvas_h; y++) {
            memcpy(opx + (y + TITLE_H) * g_width + CANVAS_X, cpx + y * canvas_w,
                   (size_t)canvas_w * sizeof(uint32_t));
        }
    }

    /* Render dialog overlay if active */
    if (g_show_dialog) {
        render_dialog(g_overlay);
    }

    uint32_t *fb = bot_window_get_framebuffer(g_window);
    if (fb) memcpy(fb, opx, (size_t)(g_width * g_height) * sizeof(uint32_t));
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

/* ── Event Handlers ──────────────────────────────────────── */

static void on_mouse_down(const bot_event_t *ev, void *data)
{
    (void)data;
    int mx = ev->mouse.x, my = ev->mouse.y;

    if (g_show_dialog) {
        on_dialog_mouse_down(mx, my);
        return;
    }

    /* Toolbar clicks */
    if (mx < TOOLBAR_WIDTH) {
        if (my < TITLE_H) return;
        /* Tool buttons */
        for (int i = 0; i < TOOL_COUNT; i++) {
            if (bot_button_hit(4, TITLE_H + 4 + i * TOOL_BTN_GAP, TOOL_BTN_W, TOOL_BTN_H, mx, my)) {
                g_tool = (tool_t)i;
                g_needs_redraw = 1;
                return;
            }
        }
        /* OPEN button */
        if (bot_button_hit(4, TITLE_H + 4 + 5 * TOOL_BTN_GAP, TOOL_BTN_W, TOOL_BTN_H, mx, my)) {
            g_show_dialog = 1;
            g_dialog_cursor = (int)strlen(g_dialog_filename);
            g_needs_redraw = 1;
            return;
        }
        /* SAVE button */
        if (bot_button_hit(4, TITLE_H + 4 + 6 * TOOL_BTN_GAP, TOOL_BTN_W, TOOL_BTN_H, mx, my)) {
            g_show_dialog = 2;
            g_dialog_cursor = (int)strlen(g_dialog_filename);
            g_needs_redraw = 1;
            return;
        }
        /* Palette clicks */
        int py_base = TITLE_H + 12 + 7 * TOOL_BTN_GAP;
        for (int i = 0; i < PALETTE_COUNT; i++) {
            int px = 8 + (i%2)*26, py = py_base + (i/2)*26;
            if (bot_button_hit(px, py, SWATCH_SIZE, SWATCH_SIZE, mx, my)) {
                g_color_idx = i;
                g_brush = g_palette[i];
                g_needs_redraw = 1;
                return;
            }
        }
        return;
    }

    if (my < TITLE_H) return;

    int cx = mx - CANVAS_X, cy = my - TITLE_H;
    save_undo();
    g_drawing = 1;
    g_drag_x = g_last_x = cx;
    g_drag_y = g_last_y = cy;

    if (g_tool == TOOL_PEN) {
        thick_pixel(g_canvas, cx, cy, g_brush, g_brush_size);
        g_needs_redraw = 1;
    } else if (g_tool == TOOL_FILL) {
        flood_fill(g_canvas, cx, cy, g_brush);
        g_drawing = 0;
        g_needs_redraw = 1;
    }
}

static void on_mouse_move(const bot_event_t *ev, void *data)
{
    (void)data;
    if (g_show_dialog) return;
    if (!g_drawing) return;
    int cx = ev->mouse.x - CANVAS_X, cy = ev->mouse.y - TITLE_H;
    if (g_tool == TOOL_PEN) {
        int dx = cx - g_last_x, dy = cy - g_last_y;
        int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
        if (steps == 0) steps = 1;
        for (int i = 0; i <= steps; i++)
            thick_pixel(g_canvas, g_last_x + dx*i/steps, g_last_y + dy*i/steps,
                        g_brush, g_brush_size);
        g_needs_redraw = 1;
    }
    g_last_x = cx;
    g_last_y = cy;
}

static void on_mouse_up(const bot_event_t *ev, void *data)
{
    (void)data;
    if (g_show_dialog) return;
    if (!g_drawing) return;
    int cx = ev->mouse.x - CANVAS_X, cy = ev->mouse.y - TITLE_H;

    switch (g_tool) {
        case TOOL_LINE:
            bot_canvas_draw_line(g_canvas, g_drag_x, g_drag_y, cx, cy, g_brush);
            break;
        case TOOL_RECT: {
            int rx = g_drag_x < cx ? g_drag_x : cx, ry = g_drag_y < cy ? g_drag_y : cy;
            bot_canvas_draw_rect(g_canvas, rx, ry, abs(cx-g_drag_x), abs(cy-g_drag_y), g_brush);
            break;
        }
        case TOOL_CIRCLE: {
            int dx = cx-g_drag_x, dy = cy-g_drag_y;
            int r = (int)__builtin_sqrt((double)(dx*dx+dy*dy));
            bot_canvas_draw_circle(g_canvas, g_drag_x, g_drag_y, r, g_brush);
            break;
        }
        default: break;
    }
    g_drawing = 0;
    g_needs_redraw = 1;
}

static void on_key_down(const bot_event_t *ev, void *data)
{
    (void)data;
    int key = ev->key.keycode;

    if (g_show_dialog) {
        on_dialog_key_down(key, ev->key.modifiers);
        return;
    }
    switch (key) {
        case 'p': g_tool = TOOL_PEN;    g_needs_redraw = 1; break;
        case 'l': g_tool = TOOL_LINE;   g_needs_redraw = 1; break;
        case 'r': g_tool = TOOL_RECT;   g_needs_redraw = 1; break;
        case 'o': g_tool = TOOL_CIRCLE; g_needs_redraw = 1; break;
        case 'f': g_tool = TOOL_FILL;   g_needs_redraw = 1; break;
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9': case '0': {
            int idx = (key == '0') ? 9 : key - '1';
            g_color_idx = idx; g_brush = g_palette[idx]; g_needs_redraw = 1;
            break;
        }
        case '=': if (g_brush_size < 20) { g_brush_size++; g_needs_redraw = 1; } break;
        case '-': if (g_brush_size > 1)  { g_brush_size--; g_needs_redraw = 1; } break;
        case 'c': save_undo(); bot_canvas_clear(g_canvas, BOT_COLOR_BLACK); g_needs_redraw = 1; break;
        case 'z': if (ev->key.modifiers & 2) do_undo(); break;
        case BOT_KEY_ESCAPE: bot_event_quit(); break;
    }
}

static void on_resize(const bot_event_t *ev, void *data)
{
    (void)data;
    int new_w = ev->resize.width;
    int new_h = ev->resize.height;

    /* See botterm.c's on_resize for why this call is required: without
     * it, window.c's framebuffer stays the old size and the blit at
     * the end of render() overflows it as soon as anything external
     * (the WM) resizes this window. */
    bot_window_resize(g_window, new_w, new_h);

    bot_canvas_destroy(g_overlay);
    g_overlay = bot_canvas_create(new_w, new_h);

    int new_canvas_w = new_w - TOOLBAR_WIDTH;
    int new_canvas_h = new_h - TITLE_H;
    bot_canvas_t *new_canvas = bot_canvas_create(new_canvas_w, new_canvas_h);
    bot_canvas_clear(new_canvas, BOT_COLOR_BLACK);

    int old_canvas_w = bot_canvas_width(g_canvas);
    int old_canvas_h = bot_canvas_height(g_canvas);
    int copy_w = old_canvas_w < new_canvas_w ? old_canvas_w : new_canvas_w;
    int copy_h = old_canvas_h < new_canvas_h ? old_canvas_h : new_canvas_h;

    uint32_t *new_px = bot_canvas_get_pixels(new_canvas);
    uint32_t *old_px = bot_canvas_get_pixels(g_canvas);
    if (new_px && old_px) {
        for (int y = 0; y < copy_h; y++) {
            memcpy(new_px + y * new_canvas_w, old_px + y * old_canvas_w, (size_t)copy_w * sizeof(uint32_t));
        }
    }

    bot_canvas_destroy(g_canvas);
    g_canvas = new_canvas;

    g_width = new_w;
    g_height = new_h;

    free(g_undo_buf);
    g_undo_buf = NULL;

    g_needs_redraw = 1;
}

/* ── Main ────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    bot_log_init(NULL, BOT_LOG_INFO);

    int loaded = 0;
    if (argc > 1) {
        strncpy(g_dialog_filename, argv[1], sizeof(g_dialog_filename) - 1);
        g_dialog_filename[sizeof(g_dialog_filename) - 1] = '\0';
        
        char resolved[256];
        if (argv[1][0] != '/') {
            snprintf(resolved, sizeof(resolved), "/root/%s", argv[1]);
        } else {
            strncpy(resolved, argv[1], sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
        }

        FILE *f = fopen(resolved, "r");
        if (f) {
            char header[32];
            if (fgets(header, sizeof(header), f)) {
                header[strcspn(header, "\r\n")] = '\0';
                if (strcmp(header, "BOTIMG_ASCII") == 0) {
                    uint32_t w = 0, h = 0;
                    if (fscanf(f, "%u %u", &w, &h) == 2 && w > 0 && h > 0) {
                        g_width = (int)w + TOOLBAR_WIDTH;
                        g_height = (int)h + TITLE_H;
                    }
                } else {
                    fseek(f, 0, SEEK_SET);
                    uint32_t magic = 0;
                    if (fread(&magic, sizeof(magic), 1, f) == 1 && magic == 0x474D4942) {
                        uint32_t w = 0, h = 0;
                        if (fread(&w, sizeof(w), 1, f) == 1 && fread(&h, sizeof(h), 1, f) == 1) {
                            if (w > 0 && h > 0) {
                                g_width = (int)w + TOOLBAR_WIDTH;
                                g_height = (int)h + TITLE_H;
                            }
                        }
                    }
                }
            }
            fclose(f);
        }
    }

    if (bot_ui_init() != 0) return 1;

    g_window  = bot_window_create("BotPaint", g_width, g_height);
    g_canvas  = bot_canvas_create(g_width - TOOLBAR_WIDTH, g_height - TITLE_H);
    g_overlay = bot_canvas_create(g_width, g_height);

    if (argc > 1) {
        if (botpaint_load(argv[1]) == 0) {
            loaded = 1;
        }
    }
    if (!g_window || !g_canvas || !g_overlay) return 1;

    if (!loaded) {
        bot_canvas_clear(g_canvas, BOT_COLOR_BLACK);
    }

    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_MOUSE_MOVE, on_mouse_move, NULL);
    bot_event_on(BOT_EVENT_MOUSE_UP,   on_mouse_up,   NULL);
    bot_event_on(BOT_EVENT_KEY_DOWN,   on_key_down,    NULL);
    bot_event_on(BOT_EVENT_RESIZE,     on_resize,     NULL);

    bot_window_show(g_window);
    render_frame();

    bot_event_t event;
    while (!bot_window_should_close(g_window)) {
        while (bot_event_poll(&event))
            if (event.type == BOT_EVENT_CLOSE) goto cleanup;
        if (g_needs_redraw) render_frame();
        bot_event_wait(&event);
        if (event.type == BOT_EVENT_CLOSE) break;
    }

cleanup:
    free(g_undo_buf);
    bot_canvas_destroy(g_overlay);
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    bot_log_shutdown();
    return 0;
}
