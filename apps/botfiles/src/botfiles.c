/* ============================================================
 * BotOS Core — BotFiles File Manager (Production)
 * ============================================================
 * File:    botfiles.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Visual file explorer.
 *
 * 2026-07 update: this used to only ever show /root, read-only —
 * double-clicking a directory did nothing, and there was no way to
 * create, rename, or delete anything. It now navigates real
 * directory trees (double-click to enter, Up button / Backspace to
 * go to the parent) and has New Folder / Rename / Delete, each
 * confirmed via bot_widget's existing modal pattern before anything
 * destructive happens. App launching is now non-blocking too, so
 * opening a file doesn't freeze the file manager itself — under
 * BotDesk's window manager it opens as its own concurrent window.
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
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define TITLE_H          24
#define STATUS_HEIGHT    22
#define TOOLBAR_H         30
#define ICON_SIZE        48
#define ICON_SPACING     80
#define ICON_GRID_X      30
#define ICON_GRID_Y      20

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
static char g_current_path[512] = "/root";

static bot_window_t *g_window = NULL;
static bot_canvas_t *g_canvas = NULL;
static int g_needs_redraw = 1;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static uint32_t g_last_click_time = 0;
static int g_last_clicked_idx = -1;
static int g_selected_idx = -1;

/* Text-input mode, shared by New Folder and Rename (same pattern
 * botpkg_gui.c already uses for its search box). */
typedef enum { INPUT_NONE = 0, INPUT_NEW_FOLDER, INPUT_RENAME } input_mode_t;
static input_mode_t g_input_mode = INPUT_NONE;
static char g_input_buf[128] = "";
static int  g_input_cursor = 0;

/* Confirm-before-delete modal, mirroring BotDesk's alert modal. */
static int  g_confirm_delete = 0;
static char g_confirm_msg[300] = "";

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
        g_files[g_file_count].name[sizeof(g_files[g_file_count].name) - 1] = '\0';

        int is_dir = (dir->d_type == DT_DIR);
        if (dir->d_type == DT_UNKNOWN) {
            /* Some filesystems never fill in d_type — fall back to stat(). */
            char full[768];
            snprintf(full, sizeof(full), "%s/%s", path, dir->d_name);
            struct stat st;
            if (stat(full, &st) == 0) is_dir = S_ISDIR(st.st_mode);
        }
        g_files[g_file_count].is_dir = is_dir;

        if (is_dir) {
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

    g_selected_idx = -1;
    g_last_clicked_idx = -1;
}

static void navigate_to(const char *path)
{
    strncpy(g_current_path, path, sizeof(g_current_path) - 1);
    g_current_path[sizeof(g_current_path) - 1] = '\0';
    scan_directory(g_current_path);
    g_needs_redraw = 1;
}

static void navigate_up(void)
{
    if (strcmp(g_current_path, "/") == 0) return;
    char *slash = strrchr(g_current_path, '/');
    if (slash == g_current_path) {
        g_current_path[1] = '\0'; /* parent of /something is / */
    } else if (slash) {
        *slash = '\0';
    }
    scan_directory(g_current_path);
    g_needs_redraw = 1;
}

/* Non-blocking: fork and keep going, rather than tearing this window
 * down and blocking on the child. Under BotDesk's window manager the
 * launched app gets decorated and shown as its own window while
 * BotFiles keeps running; without a WM it still opens (undecorated),
 * it just won't have WM chrome — either way BotFiles itself no
 * longer freezes while something else is open. */
static void launch_app(const char *path, char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp(path, argv);
        _exit(127);
    }
}

static void reap_children(void)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) { /* just prevent zombies */ }
}

static void render_files(void)
{
    bot_theme_t *th = bot_theme_get();
    bot_canvas_clear(g_canvas, th->bg);

    char title[560];
    snprintf(title, sizeof(title), "BotFiles - %s", g_current_path);
    bot_draw_window_frame(g_canvas, 0, 0, g_width, g_height, title, TITLE_H);

    /* Toolbar */
    bot_draw_panel(g_canvas, 0, TITLE_H, g_width, TOOLBAR_H);
    bot_draw_separator(g_canvas, 0, TITLE_H + TOOLBAR_H, g_width);
    int tb_y = TITLE_H + 3;
    bot_draw_button(g_canvas, 6,   tb_y, 60, TOOLBAR_H - 6, "Up", BOT_BTN_NORMAL);
    bot_draw_button(g_canvas, 70,  tb_y, 100, TOOLBAR_H - 6, "New Folder", BOT_BTN_NORMAL);
    bot_draw_button(g_canvas, 174, tb_y, 90, TOOLBAR_H - 6, "Rename",
                    g_selected_idx >= 0 ? BOT_BTN_NORMAL : BOT_BTN_HOVER);
    bot_draw_button(g_canvas, 268, tb_y, 90, TOOLBAR_H - 6, "Delete", BOT_BTN_NORMAL);

    int grid_top = TITLE_H + TOOLBAR_H;
    int cols = (g_width - ICON_GRID_X * 2) / (ICON_SIZE + ICON_SPACING) + 1;
    if (cols <= 0) cols = 1;

    for (int i = 0; i < g_file_count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x1 = ICON_GRID_X + col * (ICON_SIZE + ICON_SPACING);
        int y1 = grid_top + ICON_GRID_Y + row * (ICON_SIZE + ICON_SPACING);
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20;

        int hovered  = (g_mouse_x >= x1 && g_mouse_x < x2 && g_mouse_y >= y1 && g_mouse_y < y2);
        int selected = (i == g_selected_idx);

        if (selected) {
            bot_canvas_fill_rect(g_canvas, x1 - 6, y1 - 6, ICON_SIZE + 12, ICON_SIZE + 28,
                                 BOT_RGBA(th->accent >> 16 & 0xFF, th->accent >> 8 & 0xFF, th->accent & 0xFF, 70));
        } else if (hovered) {
            bot_canvas_fill_rect(g_canvas, x1 - 6, y1 - 6, ICON_SIZE + 12, ICON_SIZE + 28,
                                 BOT_RGBA(255, 255, 255, 30));
        }

        bot_draw_icon(g_canvas, x1, y1, ICON_SIZE, g_files[i].color, g_files[i].name);
    }

    bot_draw_separator(g_canvas, 0, g_height - STATUS_HEIGHT - 1, g_width);
    char status[300];
    if (g_selected_idx >= 0 && g_selected_idx < g_file_count) {
        snprintf(status, sizeof(status), " %s selected", g_files[g_selected_idx].name);
    } else {
        snprintf(status, sizeof(status), " %d items", g_file_count);
    }
    bot_draw_status_bar(g_canvas, 0, g_height - STATUS_HEIGHT, g_width, STATUS_HEIGHT, status);

    /* Text-input modal (New Folder / Rename) */
    if (g_input_mode != INPUT_NONE) {
        int box_w = 400, box_h = 110;
        int box_x = (g_width - box_w) / 2, box_y = (g_height - box_h) / 2;
        bot_canvas_fill_rect(g_canvas, 0, 0, g_width, g_height, BOT_RGBA(0, 0, 0, 110));
        bot_draw_panel(g_canvas, box_x, box_y, box_w, box_h);
        bot_draw_window_frame(g_canvas, box_x, box_y, box_w, box_h,
                              g_input_mode == INPUT_NEW_FOLDER ? "New Folder" : "Rename", 24);
        bot_draw_textbox(g_canvas, box_x + 20, box_y + 40, box_w - 40, 24, g_input_buf, g_input_cursor);
        bot_draw_text(g_canvas, box_x + 20, box_y + box_h - 26,
                     "Enter to confirm, Esc to cancel", th->fg_dim, 1);
    }

    /* Confirm-delete modal */
    if (g_confirm_delete) {
        int box_w = 420, box_h = 120;
        int box_x = (g_width - box_w) / 2, box_y = (g_height - box_h) / 2;
        bot_canvas_fill_rect(g_canvas, 0, 0, g_width, g_height, BOT_RGBA(0, 0, 0, 110));
        bot_draw_panel(g_canvas, box_x, box_y, box_w, box_h);
        bot_draw_window_frame(g_canvas, box_x, box_y, box_w, box_h, "Confirm Delete", 24);
        bot_draw_text(g_canvas, box_x + 20, box_y + 40, g_confirm_msg, th->fg, 1);
        bot_draw_button(g_canvas, box_x + 60, box_y + box_h - 36, 120, 24, "Delete", BOT_BTN_NORMAL);
        bot_draw_button(g_canvas, box_x + box_w - 180, box_y + box_h - 36, 120, 24, "Cancel", BOT_BTN_NORMAL);
    }

    uint32_t *fb = bot_window_get_framebuffer(g_window);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    if (fb && px)
        memcpy(fb, px, (size_t)(g_width * g_height) * sizeof(uint32_t));
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

static void begin_text_input(input_mode_t mode, const char *initial)
{
    g_input_mode = mode;
    strncpy(g_input_buf, initial ? initial : "", sizeof(g_input_buf) - 1);
    g_input_buf[sizeof(g_input_buf) - 1] = '\0';
    g_input_cursor = (int)strlen(g_input_buf);
    g_needs_redraw = 1;
}

static void on_mouse_down(const bot_event_t *ev, void *data)
{
    (void)data;
    int mx = ev->mouse.x, my = ev->mouse.y;

    if (g_input_mode != INPUT_NONE) return; /* modal: ignore clicks behind it */

    if (g_confirm_delete) {
        int box_w = 420, box_h = 120;
        int box_x = (g_width - box_w) / 2, box_y = (g_height - box_h) / 2;
        if (bot_button_hit(box_x + 60, box_y + box_h - 36, 120, 24, mx, my)) {
            if (g_selected_idx >= 0 && g_selected_idx < g_file_count) {
                char full[768];
                snprintf(full, sizeof(full), "%s/%s", g_current_path, g_files[g_selected_idx].name);
                if (g_files[g_selected_idx].is_dir) rmdir(full);
                else unlink(full);
                scan_directory(g_current_path);
            }
            g_confirm_delete = 0;
            g_needs_redraw = 1;
            return;
        }
        if (bot_button_hit(box_x + box_w - 180, box_y + box_h - 36, 120, 24, mx, my)) {
            g_confirm_delete = 0;
            g_needs_redraw = 1;
            return;
        }
        return;
    }

    uint32_t now = get_ticks_ms();
    int tb_y = TITLE_H + 3;

    if (bot_button_hit(6, tb_y, 60, TOOLBAR_H - 6, mx, my)) {
        navigate_up();
        return;
    }
    if (bot_button_hit(70, tb_y, 100, TOOLBAR_H - 6, mx, my)) {
        begin_text_input(INPUT_NEW_FOLDER, "");
        return;
    }
    if (bot_button_hit(174, tb_y, 90, TOOLBAR_H - 6, mx, my)) {
        if (g_selected_idx >= 0 && g_selected_idx < g_file_count) {
            begin_text_input(INPUT_RENAME, g_files[g_selected_idx].name);
        }
        return;
    }
    if (bot_button_hit(268, tb_y, 90, TOOLBAR_H - 6, mx, my)) {
        if (g_selected_idx >= 0 && g_selected_idx < g_file_count) {
            snprintf(g_confirm_msg, sizeof(g_confirm_msg), "Delete \"%s\"? This cannot be undone.",
                    g_files[g_selected_idx].name);
            g_confirm_delete = 1;
            g_needs_redraw = 1;
        }
        return;
    }

    int grid_top = TITLE_H + TOOLBAR_H;
    int cols = (g_width - ICON_GRID_X * 2) / (ICON_SIZE + ICON_SPACING) + 1;
    if (cols <= 0) cols = 1;

    for (int i = 0; i < g_file_count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x1 = ICON_GRID_X + col * (ICON_SIZE + ICON_SPACING);
        int y1 = grid_top + ICON_GRID_Y + row * (ICON_SIZE + ICON_SPACING);
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20;

        if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
            g_selected_idx = i;
            if (g_last_clicked_idx == i && (now - g_last_click_time) < 500) {
                g_last_clicked_idx = -1;

                if (g_files[i].is_dir) {
                    char newpath[768];
                    snprintf(newpath, sizeof(newpath), "%s/%s", g_current_path, g_files[i].name);
                    navigate_to(newpath);
                    return;
                }

                char filepath[768];
                snprintf(filepath, sizeof(filepath), "%s/%s", g_current_path, g_files[i].name);

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
            g_needs_redraw = 1;
            return;
        }
    }

    /* Clicked empty space: deselect. */
    g_selected_idx = -1;
    g_needs_redraw = 1;
}

static void on_mouse_move(const bot_event_t *ev, void *data)
{
    (void)data;
    g_mouse_x = ev->mouse.x;
    g_mouse_y = ev->mouse.y;
    g_needs_redraw = 1;
}

static void commit_text_input(void)
{
    if (g_input_buf[0] == '\0') { g_input_mode = INPUT_NONE; return; }

    char full[768];
    if (g_input_mode == INPUT_NEW_FOLDER) {
        snprintf(full, sizeof(full), "%s/%s", g_current_path, g_input_buf);
        mkdir(full, 0755);
    } else if (g_input_mode == INPUT_RENAME && g_selected_idx >= 0 && g_selected_idx < g_file_count) {
        char oldp[768], newp[768];
        snprintf(oldp, sizeof(oldp), "%s/%s", g_current_path, g_files[g_selected_idx].name);
        snprintf(newp, sizeof(newp), "%s/%s", g_current_path, g_input_buf);
        rename(oldp, newp);
    }
    g_input_mode = INPUT_NONE;
    scan_directory(g_current_path);
}

static void on_key_down(const bot_event_t *ev, void *data)
{
    (void)data;
    int key = ev->key.keycode;

    if (g_input_mode != INPUT_NONE) {
        if (key == BOT_KEY_ESCAPE) {
            g_input_mode = INPUT_NONE;
            g_needs_redraw = 1;
            return;
        }
        if (key == BOT_KEY_ENTER || key == '\n' || key == '\r') {
            commit_text_input();
            g_needs_redraw = 1;
            return;
        }
        if (key == BOT_KEY_BACKSPACE || key == 127) {
            int len = (int)strlen(g_input_buf);
            if (len > 0) {
                g_input_buf[len - 1] = '\0';
                g_input_cursor = len - 1;
                g_needs_redraw = 1;
            }
            return;
        }
        if (key >= 32 && key <= 126) {
            int len = (int)strlen(g_input_buf);
            if (len < (int)sizeof(g_input_buf) - 1) {
                g_input_buf[len] = (char)key;
                g_input_buf[len + 1] = '\0';
                g_input_cursor = len + 1;
                g_needs_redraw = 1;
            }
        }
        return;
    }

    if (g_confirm_delete) {
        if (key == BOT_KEY_ESCAPE) { g_confirm_delete = 0; g_needs_redraw = 1; }
        return;
    }

    if (key == BOT_KEY_ESCAPE) {
        bot_event_quit();
    } else if (key == BOT_KEY_BACKSPACE) {
        navigate_up();
    }
}

static void on_resize(const bot_event_t *ev, void *data)
{
    (void)data;
    g_width = ev->resize.width;
    g_height = ev->resize.height;
    /* See botterm.c's on_resize for why this call is required: without
     * it, window.c's framebuffer stays the old size and the blit at
     * the end of render() overflows it as soon as anything external
     * (the WM) resizes this window. */
    bot_window_resize(g_window, g_width, g_height);
    bot_canvas_destroy(g_canvas);
    g_canvas = bot_canvas_create(g_width, g_height);
    g_needs_redraw = 1;
}

int main(int argc, char *argv[])
{
    bot_log_init(NULL, BOT_LOG_INFO);

    if (bot_ui_init() != 0) return 1;

    g_window = bot_window_create("BotFiles", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (!g_window || !g_canvas) return 1;

    if (argc > 1 && argv[1][0] != '\0') {
        strncpy(g_current_path, argv[1], sizeof(g_current_path) - 1);
        g_current_path[sizeof(g_current_path) - 1] = '\0';
    }
    scan_directory(g_current_path);

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
        reap_children();
        if (g_needs_redraw) render_files();
        usleep(30000);
    }

cleanup:
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    bot_log_shutdown();
    return 0;
}
