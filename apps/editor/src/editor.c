/* ============================================================
 * BotOS Core — Text Editor (Production)
 * ============================================================
 * File:    editor.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — Production text editor.
 * Refactored to use the bot_widget toolkit for text rendering,
 * status bar, and gutter panel.
 *
 * Layout (800×600):
 *   ┌────┬──────────────────────────────────────┐
 *   │ LN │  Text Content Area                   │
 *   │  1 │  #include <stdio.h>                  │
 *   │  2 │  int main() {█                       │
 *   ├────┴──────────────────────────────────────┤
 *   │ editor.c  Ln 2, Col 14  [Modified]        │
 *   └──────────────────────────────────────────-┘
 * ============================================================ */

#include "bot_ui.h"
#include "bot.h"
#include "bot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Layout ──────────────────────────────────────────────── */

#define TITLE_H          24
#define GUTTER_WIDTH     36
#define STATUS_HEIGHT    22
#define TEXT_X           (GUTTER_WIDTH + 4)

static int g_width = 800;
static int g_height = 600;

/* ── Editor State ────────────────────────────────────────── */

#define MAX_LINES  8192
#define LINE_MAX   512

static char  g_lines[MAX_LINES][LINE_MAX];
static int   g_line_count = 0;
static int   g_cx = 0, g_cy = 0;
static int   g_scroll_y = 0;
static char  g_filename[256] = {0};
static int   g_modified = 0;

static bot_window_t *g_window = NULL;
static bot_canvas_t *g_canvas = NULL;
static int           g_needs_redraw = 1;

static int editor_save(void);

/* ── Dialog State ────────────────────────────────────────── */
static int   g_show_dialog = 0; /* 0: none, 1: save */
static char  g_dialog_filename[128] = "";
static int   g_dialog_cursor = 0;

static void on_dialog_key_down(int key)
{
    if (key == BOT_KEY_ESCAPE) {
        g_show_dialog = 0;
        g_needs_redraw = 1;
        return;
    }
    if (key == BOT_KEY_ENTER || key == '\n' || key == '\r') {
        if (strlen(g_dialog_filename) > 0) {
            strncpy(g_filename, g_dialog_filename, sizeof(g_filename) - 1);
            editor_save();
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

static void render_dialog(bot_canvas_t *c)
{
    uint32_t *px = bot_canvas_get_pixels(c);
    if (px) {
        for (int i = 0; i < g_width * g_height; i++) {
            uint32_t col = px[i];
            uint8_t a = (col >> 24) & 0xFF;
            uint8_t r = (uint8_t)(((col >> 16) & 0xFF) * 0.4f);
            uint8_t g = (uint8_t)(((col >> 8) & 0xFF) * 0.4f);
            uint8_t b = (uint8_t)(((col >> 0) & 0xFF) * 0.4f);
            px[i] = BOT_RGBA(r, g, b, a);
        }
    }

    int dw = 320;
    int dh = 120;
    int dx = (g_width - dw) / 2;
    int dy = (g_height - dh) / 2;

    bot_theme_t *th = bot_theme_get();
    bot_draw_window_frame(c, dx, dy, dw, dh, "Save File", 20);

    bot_draw_text(c, dx + 16, dy + 32, "Enter filename (e.g. /root/note.txt):", th->fg, 1);
    bot_draw_textbox(c, dx + 16, dy + 50, dw - 32, 24, g_dialog_filename, g_dialog_cursor);
    bot_draw_button(c, dx + 16, dy + 85, 130, 24, "OK", BOT_BTN_NORMAL);
    bot_draw_button(c, dx + dw - 146, dy + 85, 130, 24, "Cancel", BOT_BTN_NORMAL);
}

static void on_mouse_down(const bot_event_t *ev, void *data)
{
    (void)data;
    if (!g_show_dialog) return;

    int mx = ev->mouse.x, my = ev->mouse.y;
    int dw = 320;
    int dh = 120;
    int dx = (g_width - dw) / 2;
    int dy = (g_height - dh) / 2;

    /* OK button click */
    if (bot_button_hit(dx + 16, dy + 85, 130, 24, mx, my)) {
        if (strlen(g_dialog_filename) > 0) {
            strncpy(g_filename, g_dialog_filename, sizeof(g_filename) - 1);
            editor_save();
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

/* ── File I/O ────────────────────────────────────────────── */

static int editor_load(const char *path)
{
    char resolved[512];
    if (path[0] != '/') {
        snprintf(resolved, sizeof(resolved), "/root/%s", path);
    } else {
        strncpy(resolved, path, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    FILE *fp = fopen(resolved, "r");
    if (!fp) return -1;
    g_line_count = 0;
    while (g_line_count < MAX_LINES &&
           fgets(g_lines[g_line_count], LINE_MAX, fp)) {
        size_t len = strlen(g_lines[g_line_count]);
        while (len > 0 && (g_lines[g_line_count][len-1] == '\n' ||
                           g_lines[g_line_count][len-1] == '\r'))
            g_lines[g_line_count][--len] = '\0';
        g_line_count++;
    }
    fclose(fp);
    strncpy(g_filename, resolved, sizeof(g_filename) - 1);
    g_modified = 0;
    return 0;
}

static int editor_save(void)
{
    if (!g_filename[0]) return -1;

    char resolved[512];
    if (g_filename[0] != '/') {
        snprintf(resolved, sizeof(resolved), "/root/%s", g_filename);
    } else {
        strncpy(resolved, g_filename, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    FILE *fp = fopen(resolved, "w");
    if (!fp) return -1;
    for (int i = 0; i < g_line_count; i++)
        fprintf(fp, "%s\n", g_lines[i]);
    fclose(fp);

    strncpy(g_filename, resolved, sizeof(g_filename) - 1);
    g_filename[sizeof(g_filename) - 1] = '\0';

    g_modified = 0;
    return 0;
}

/* ── Text Editing ────────────────────────────────────────── */

static void clamp_cursor(void)
{
    if (g_cy < 0) g_cy = 0;
    if (g_cy >= g_line_count) g_cy = g_line_count - 1;
    if (g_cy < 0) g_cy = 0;
    int len = (int)strlen(g_lines[g_cy]);
    if (g_cx > len) g_cx = len;
    if (g_cx < 0) g_cx = 0;
    if (g_cy < g_scroll_y) g_scroll_y = g_cy;
    int visible_rows = (g_height - STATUS_HEIGHT - TITLE_H - 4) / BOT_CHAR_H;
    if (g_cy >= g_scroll_y + visible_rows) g_scroll_y = g_cy - visible_rows + 1;
}

static void insert_char(char ch)
{
    int len = (int)strlen(g_lines[g_cy]);
    if (len >= LINE_MAX - 2) return;
    memmove(g_lines[g_cy] + g_cx + 1, g_lines[g_cy] + g_cx,
            (size_t)(len - g_cx + 1));
    g_lines[g_cy][g_cx] = ch;
    g_cx++;
    g_modified = 1;
}

static void delete_char(void)
{
    if (g_cx > 0) {
        int len = (int)strlen(g_lines[g_cy]);
        memmove(g_lines[g_cy] + g_cx - 1, g_lines[g_cy] + g_cx,
                (size_t)(len - g_cx + 1));
        g_cx--;
        g_modified = 1;
    } else if (g_cy > 0) {
        int prev_len = (int)strlen(g_lines[g_cy - 1]);
        g_cx = prev_len;
        strncat(g_lines[g_cy - 1], g_lines[g_cy], LINE_MAX - prev_len - 1);
        memmove(g_lines + g_cy, g_lines + g_cy + 1,
                (size_t)(g_line_count - g_cy - 1) * LINE_MAX);
        g_line_count--;
        g_cy--;
        g_modified = 1;
    }
}

static void insert_newline(void)
{
    if (g_line_count >= MAX_LINES - 1) return;
    memmove(g_lines + g_cy + 2, g_lines + g_cy + 1,
            (size_t)(g_line_count - g_cy - 1) * LINE_MAX);
    g_line_count++;
    strncpy(g_lines[g_cy + 1], g_lines[g_cy] + g_cx, LINE_MAX - 1);
    g_lines[g_cy][g_cx] = '\0';
    g_cy++;
    g_cx = 0;
    g_modified = 1;
}

/* ── Rendering (Widget-Based) ────────────────────────────── */

static void render_editor(void)
{
    bot_theme_t *th = bot_theme_get();

    bot_canvas_clear(g_canvas, th->bg);

    /* Draw window frame */
    bot_draw_window_frame(g_canvas, 0, 0, g_width, g_height, "Editor", TITLE_H);

    /* Gutter panel starting below title bar */
    bot_draw_panel(g_canvas, 0, TITLE_H, GUTTER_WIDTH, g_height - STATUS_HEIGHT - TITLE_H);

    int visible_rows = (g_height - STATUS_HEIGHT - TITLE_H - 4) / BOT_CHAR_H;
    int visible_cols = (g_width - TEXT_X - 4) / BOT_CHAR_W;

    /* Text lines starting below title bar */
    for (int i = 0; i < visible_rows && (g_scroll_y + i) < g_line_count; i++) {
        int line_idx = g_scroll_y + i;
        int py = TITLE_H + 2 + i * BOT_CHAR_H;

        /* Current line highlight */
        if (line_idx == g_cy) {
            bot_canvas_fill_rect(g_canvas, GUTTER_WIDTH, py,
                                 g_width - GUTTER_WIDTH, BOT_CHAR_H,
                                 th->highlight);
        }

        /* Line number */
        char num[16];
        snprintf(num, sizeof(num), "%3d", line_idx + 1);
        bot_draw_text(g_canvas, 4, py + 1, num, th->fg_dim, 1);

        /* Line text */
        const char *line = g_lines[line_idx];
        int px = TEXT_X;
        for (int j = 0; line[j] && j < visible_cols; j++) {
            bot_draw_char(g_canvas, px, py + 1, line[j], th->fg, 1);
            px += BOT_CHAR_W;
        }

        /* Cursor */
        if (line_idx == g_cy) {
            int cur_px = TEXT_X + g_cx * BOT_CHAR_W;
            bot_canvas_fill_rect(g_canvas, cur_px, py, 2, BOT_CHAR_H, th->cursor);
        }
    }

    /* Separator above status bar */
    bot_draw_separator(g_canvas, 0, g_height - STATUS_HEIGHT - 1, g_width);

    /* Status bar */
    char status[512];
    snprintf(status, sizeof(status), " %s  LN %d COL %d  %s  %d LINES",
             g_filename[0] ? g_filename : "[NEW]",
             g_cy + 1, g_cx + 1,
             g_modified ? "[MODIFIED]" : "",
             g_line_count);
    bot_draw_status_bar(g_canvas, 0, g_height - STATUS_HEIGHT,
                        g_width, STATUS_HEIGHT, status);

    if (g_show_dialog) {
        render_dialog(g_canvas);
    }

    /* Blit to window */
    uint32_t *fb = bot_window_get_framebuffer(g_window);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    if (fb && px)
        memcpy(fb, px, (size_t)(g_width * g_height) * sizeof(uint32_t));
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

/* ── Event Handlers ──────────────────────────────────────── */

static void on_key(const bot_event_t *ev, void *data)
{
    (void)data;
    int key = ev->key.keycode;
    int ctrl = ev->key.modifiers & 2;

    if (g_show_dialog) {
        on_dialog_key_down(key);
        return;
    }

    if (ctrl) {
        switch (key) {
            case 's':
                if (g_filename[0]) {
                    editor_save();
                } else {
                    g_show_dialog = 1;
                    g_dialog_cursor = (int)strlen(g_dialog_filename);
                }
                g_needs_redraw = 1;
                return;
            case 'q': bot_event_quit(); return;
        }
        return;
    }

    switch (key) {
        case BOT_KEY_UP:        g_cy--; break;
        case BOT_KEY_DOWN:      g_cy++; break;
        case BOT_KEY_LEFT:      g_cx--; break;
        case BOT_KEY_RIGHT:     g_cx++; break;
        case BOT_KEY_ENTER:     insert_newline(); break;
        case BOT_KEY_BACKSPACE: delete_char(); break;
        case BOT_KEY_TAB:
            for (int i = 0; i < 4; i++) insert_char(' ');
            break;
        case BOT_KEY_ESCAPE: bot_event_quit(); return;
        default:
            if (key >= 32 && key < 127) insert_char((char)key);
            break;
    }

    clamp_cursor();
    g_needs_redraw = 1;
}

static void on_resize(const bot_event_t *ev, void *data)
{
    (void)data;
    g_width = ev->resize.width;
    g_height = ev->resize.height;
    bot_canvas_destroy(g_canvas);
    g_canvas = bot_canvas_create(g_width, g_height);
    clamp_cursor();
    g_needs_redraw = 1;
}

/* ── Main ────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    bot_log_init(NULL, BOT_LOG_INFO);
    memset(g_lines, 0, sizeof(g_lines));
    g_line_count = 1;

    if (argc >= 2) editor_load(argv[1]);

    if (bot_ui_init() != 0) return 1;

    g_window = bot_window_create("BotOS Editor", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (!g_window || !g_canvas) return 1;

    bot_event_on(BOT_EVENT_KEY_DOWN, on_key, NULL);
    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_RESIZE,   on_resize, NULL);
    bot_window_show(g_window);
    render_editor();

    bot_event_t event;
    while (!bot_window_should_close(g_window)) {
        while (bot_event_poll(&event))
            if (event.type == BOT_EVENT_CLOSE) goto done;
        if (g_needs_redraw) render_editor();
        bot_event_wait(&event);
        if (event.type == BOT_EVENT_CLOSE) break;
    }

done:
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    bot_log_shutdown();
    return 0;
}
