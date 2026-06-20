/* ============================================================
 * BotOS Core — BotSettings (Production)
 * ============================================================
 * File:    botsettings.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * System configurations manager for theme and color selection.
 * ============================================================ */

#include "bot_ui.h"
#include "bot_log.h"
#include "bot_widget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TITLE_H          24
#define STATUS_HEIGHT    22

static int g_width = 400;
static int g_height = 300;

static bot_window_t *g_window = NULL;
static bot_canvas_t *g_canvas = NULL;
static int g_needs_redraw = 1;

static char g_selected_theme[32] = "dark";

static void load_current_theme(void)
{
    FILE *f = fopen("/etc/botos/theme.conf", "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            val[strcspn(val, "\r\n")] = '\0';
            if (strcmp(key, "theme") == 0) {
                strncpy(g_selected_theme, val, sizeof(g_selected_theme) - 1);
            }
        }
    }
    fclose(f);
}

static void save_theme(const char *theme_name)
{
    mkdir("/etc/botos", 0755);
    FILE *f = fopen("/etc/botos/theme.conf", "w");
    if (!f) return;

    fprintf(f, "theme=%s\n", theme_name);
    fclose(f);

    strncpy(g_selected_theme, theme_name, sizeof(g_selected_theme) - 1);
    bot_theme_load();
}

static void render_settings(void)
{
    bot_theme_t *th = bot_theme_get();
    bot_canvas_clear(g_canvas, th->bg);

    bot_draw_window_frame(g_canvas, 0, 0, g_width, g_height, "System Settings", TITLE_H);

    bot_draw_text(g_canvas, 20, TITLE_H + 20, "Select Color Theme:", th->fg, 1);

    int btn_w = 360;
    int btn_h = 32;
    int start_y = TITLE_H + 45;

    bot_btn_state_t dark_st = (strcmp(g_selected_theme, "dark") == 0) ? BOT_BTN_ACTIVE : BOT_BTN_NORMAL;
    bot_btn_state_t light_st = (strcmp(g_selected_theme, "light") == 0) ? BOT_BTN_ACTIVE : BOT_BTN_NORMAL;
    bot_btn_state_t matrix_st = (strcmp(g_selected_theme, "matrix") == 0) ? BOT_BTN_ACTIVE : BOT_BTN_NORMAL;

    bot_draw_button(g_canvas, 20, start_y, btn_w, btn_h, "Dark Charcoal (Default)", dark_st);
    bot_draw_button(g_canvas, 20, start_y + 40, btn_w, btn_h, "Light Lavender", light_st);
    bot_draw_button(g_canvas, 20, start_y + 80, btn_w, btn_h, "Digital Matrix Green", matrix_st);

    bot_draw_separator(g_canvas, 0, g_height - STATUS_HEIGHT - 35, g_width);
    bot_draw_button(g_canvas, 20, g_height - STATUS_HEIGHT - 30, 160, 24, "Apply & Exit", BOT_BTN_NORMAL);
    bot_draw_button(g_canvas, 220, g_height - STATUS_HEIGHT - 30, 160, 24, "Cancel", BOT_BTN_NORMAL);

    bot_draw_separator(g_canvas, 0, g_height - STATUS_HEIGHT - 1, g_width);
    char status[128];
    snprintf(status, sizeof(status), " Active theme: %s", g_selected_theme);
    bot_draw_status_bar(g_canvas, 0, g_height - STATUS_HEIGHT, g_width, STATUS_HEIGHT, status);

    uint32_t *fb = bot_window_get_framebuffer(g_window);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    if (fb && px)
        memcpy(fb, px, (size_t)(g_width * g_height) * sizeof(uint32_t));
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

static void on_mouse_down(const bot_event_t *ev, void *data)
{
    (void)data;
    int mx = ev->mouse.x, my = ev->mouse.y;
    int start_y = TITLE_H + 45;

    if (bot_button_hit(20, start_y, 360, 32, mx, my)) {
        save_theme("dark");
        g_needs_redraw = 1;
    }
    else if (bot_button_hit(20, start_y + 40, 360, 32, mx, my)) {
        save_theme("light");
        g_needs_redraw = 1;
    }
    else if (bot_button_hit(20, start_y + 80, 360, 32, mx, my)) {
        save_theme("matrix");
        g_needs_redraw = 1;
    }
    else if (bot_button_hit(20, g_height - STATUS_HEIGHT - 30, 160, 24, mx, my)) {
        bot_event_quit();
    }
    else if (bot_button_hit(220, g_height - STATUS_HEIGHT - 30, 160, 24, mx, my)) {
        bot_event_quit();
    }
}

static void on_key_down(const bot_event_t *ev, void *data)
{
    (void)data;
    if (ev->key.keycode == BOT_KEY_ESCAPE) {
        bot_event_quit();
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    bot_log_init(NULL, BOT_LOG_INFO);

    if (bot_ui_init() != 0) return 1;

    g_window = bot_window_create("BotSettings", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (!g_window || !g_canvas) return 1;

    load_current_theme();

    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_KEY_DOWN, on_key_down, NULL);

    bot_window_show(g_window);
    render_settings();

    bot_event_t event;
    while (!bot_window_should_close(g_window)) {
        while (bot_event_poll(&event))
            if (event.type == BOT_EVENT_CLOSE) goto cleanup;
        if (g_needs_redraw) render_settings();
        bot_event_wait(&event);
        if (event.type == BOT_EVENT_CLOSE) break;
    }

cleanup:
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    bot_log_shutdown();
    return 0;
}
