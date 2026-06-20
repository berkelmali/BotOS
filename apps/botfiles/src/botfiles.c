/* ============================================================
 * BotOS Core — BotFiles File Manager (Production)
 * ============================================================
 * File:    botfiles.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Visual file explorer listing files in /root directory.
 * ============================================================ */

#include "bot_ui.h"
#include "bot_log.h"
#include "bot_widget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define TITLE_H          24
#define STATUS_HEIGHT    22
#define ICON_SIZE        48
#define ICON_SPACING     80
#define ICON_GRID_X      30
#define ICON_GRID_Y      40

static int g_width = 800;
static int g_height = 600;

typedef struct {
    char name[128];
    int is_dir;
    bot_color_t color;
} file_entry_t;

#define MAX_FILES 256
static file_entry_t g_files[MAX_FILES];
static int g_file_count = 0;

static bot_window_t *g_window = NULL;
static bot_canvas_t *g_canvas = NULL;
static int g_needs_redraw = 1;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static uint32_t g_last_click_time = 0;
static int g_last_clicked_idx = -1;

static uint32_t get_ticks_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void scan_directory(const char *path)
{
    g_file_count = 0;
    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL && g_file_count < MAX_FILES) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;

        strncpy(g_files[g_file_count].name, dir->d_name, sizeof(g_files[g_file_count].name) - 1);
        g_files[g_file_count].is_dir = (dir->d_type == DT_DIR);
        
        if (g_files[g_file_count].is_dir) {
            g_files[g_file_count].color = BOT_RGBA(255, 200, 60, 255);
        } else {
            char *ext = strrchr(dir->d_name, '.');
            if (ext && strcmp(ext, ".txt") == 0) {
                g_files[g_file_count].color = BOT_RGBA(80, 160, 255, 255);
            } else if (ext && strcmp(ext, ".botimg") == 0) {
                g_files[g_file_count].color = BOT_RGBA(255, 120, 80, 255);
            } else {
                g_files[g_file_count].color = BOT_RGBA(160, 160, 160, 255);
            }
        }
        g_file_count++;
    }
    closedir(d);
}

static void launch_app(const char *path, char *const argv[])
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
    g_window = bot_window_create("BotFiles", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (g_window) bot_window_show(g_window);
    scan_directory("/root");
    g_needs_redraw = 1;
}

static void render_files(void)
{
    bot_theme_t *th = bot_theme_get();
    bot_canvas_clear(g_canvas, th->bg);

    bot_draw_window_frame(g_canvas, 0, 0, g_width, g_height, "BotFiles - /root", TITLE_H);

    int cols = (g_width - ICON_GRID_X * 2) / (ICON_SIZE + ICON_SPACING) + 1;
    if (cols <= 0) cols = 1;

    for (int i = 0; i < g_file_count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x1 = ICON_GRID_X + col * (ICON_SIZE + ICON_SPACING);
        int y1 = TITLE_H + ICON_GRID_Y + row * (ICON_SIZE + ICON_SPACING);
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20;

        int hovered = (g_mouse_x >= x1 && g_mouse_x < x2 &&
                       g_mouse_y >= y1 && g_mouse_y < y2);

        if (hovered) {
            bot_canvas_fill_rect(g_canvas, x1 - 6, y1 - 6, ICON_SIZE + 12, ICON_SIZE + 28,
                                 BOT_RGBA(255, 255, 255, 30));
        }

        bot_draw_icon(g_canvas, x1, y1, ICON_SIZE, g_files[i].color, g_files[i].name);
    }

    bot_draw_separator(g_canvas, 0, g_height - STATUS_HEIGHT - 1, g_width);
    char status[128];
    snprintf(status, sizeof(status), " %d files found in /root", g_file_count);
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
    uint32_t now = get_ticks_ms();

    int cols = (g_width - ICON_GRID_X * 2) / (ICON_SIZE + ICON_SPACING) + 1;
    if (cols <= 0) cols = 1;

    for (int i = 0; i < g_file_count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x1 = ICON_GRID_X + col * (ICON_SIZE + ICON_SPACING);
        int y1 = TITLE_H + ICON_GRID_Y + row * (ICON_SIZE + ICON_SPACING);
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20;

        if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
            if (g_last_clicked_idx == i && (now - g_last_click_time) < 500) {
                g_last_clicked_idx = -1;
                
                char filepath[256];
                snprintf(filepath, sizeof(filepath), "/root/%s", g_files[i].name);
                
                char *ext = strrchr(g_files[i].name, '.');
                if (ext && strcmp(ext, ".txt") == 0) {
                    char *argv[] = { "/usr/bin/editor", filepath, NULL };
                    launch_app(argv[0], argv);
                } else if (ext && strcmp(ext, ".botimg") == 0) {
                    char *argv[] = { "/usr/bin/botpaint", filepath, NULL };
                    launch_app(argv[0], argv);
                }
            } else {
                g_last_clicked_idx = i;
                g_last_click_time = now;
            }
            break;
        }
    }
}

static void on_mouse_move(const bot_event_t *ev, void *data)
{
    (void)data;
    g_mouse_x = ev->mouse.x;
    g_mouse_y = ev->mouse.y;
    g_needs_redraw = 1;
}

static void on_key_down(const bot_event_t *ev, void *data)
{
    (void)data;
    if (ev->key.keycode == BOT_KEY_ESCAPE) {
        bot_event_quit();
    }
}

static void on_resize(const bot_event_t *ev, void *data)
{
    (void)data;
    g_width = ev->resize.width;
    g_height = ev->resize.height;
    bot_canvas_destroy(g_canvas);
    g_canvas = bot_canvas_create(g_width, g_height);
    g_needs_redraw = 1;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    bot_log_init(NULL, BOT_LOG_INFO);

    if (bot_ui_init() != 0) return 1;

    g_window = bot_window_create("BotFiles", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (!g_window || !g_canvas) return 1;

    scan_directory("/root");

    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_MOUSE_MOVE, on_mouse_move, NULL);
    bot_event_on(BOT_EVENT_KEY_DOWN, on_key_down, NULL);
    bot_event_on(BOT_EVENT_RESIZE, on_resize, NULL);

    bot_window_show(g_window);
    render_files();

    bot_event_t event;
    while (!bot_window_should_close(g_window)) {
        while (bot_event_poll(&event))
            if (event.type == BOT_EVENT_CLOSE) goto cleanup;
        if (g_needs_redraw) render_files();
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
