/* ============================================================
 * BotOS Core — BotPkg GUI Package Manager (Production)
 * ============================================================
 * File:    botpkg_gui.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Graphical frontend for the botpkg package manager CLI.
 * ============================================================ */

#include "bot_ui.h"
#include "bot_log.h"
#include "bot_widget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define TITLE_H          24
#define STATUS_HEIGHT    22

static int g_width = 600;
static int g_height = 480;

static bot_window_t *g_window = NULL;
static bot_canvas_t *g_canvas = NULL;
static int g_needs_redraw = 1;

#define MAX_ITEMS 128
static char g_items[MAX_ITEMS][128];
static int g_item_count = 0;
static int g_selected_idx = -1;

static char g_search_query[64] = "";
static int g_search_cursor = 0;
static int g_search_active = 0;

static char g_status_msg[128] = " Ready";

static void strip_ansi(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '\033') {
            src++;
            if (*src == '[') {
                src++;
                while (*src && ((*src >= '0' && *src <= '9') || *src == ';')) src++;
                if (*src) src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* Runs the `botpkg` CLI directly via fork+execvp and captures its
 * stdout/stderr through a pipe.
 *
 * `argv` must be a NULL-terminated array suitable for execvp(), with
 * argv[0] == "botpkg" (e.g. {"botpkg", "search", query, NULL}). This
 * intentionally never goes through a shell: previously this built a
 * command *string* (e.g. "botpkg search %s" with the search box text
 * spliced in) and handed it to popen(), which runs it via `/bin/sh -c`
 * — so any shell metacharacter typed into the search box (";", "|",
 * "$(...)", "&&", backticks, ...) was interpreted by the shell,
 * letting anyone with access to this GUI run arbitrary commands.
 * Passing each argument through execvp's argv array means the
 * contents are always treated as a single literal string argument to
 * botpkg, never as shell syntax. */
static void run_botpkg_cmd(char *const argv[])
{
    g_item_count = 0;
    g_selected_idx = -1;
    strncpy(g_status_msg, " Running command...", sizeof(g_status_msg)-1);
    g_status_msg[sizeof(g_status_msg)-1] = '\0';

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        strncpy(g_status_msg, " Error executing command", sizeof(g_status_msg)-1);
        g_status_msg[sizeof(g_status_msg)-1] = '\0';
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        strncpy(g_status_msg, " Error executing command", sizeof(g_status_msg)-1);
        g_status_msg[sizeof(g_status_msg)-1] = '\0';
        return;
    }

    if (pid == 0) {
        /* Child: send both stdout and stderr down the pipe (matching
         * the errors-visible-in-output behavior popen("r") gave us),
         * then replace this process with botpkg. No shell involved. */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp("botpkg", argv);
        _exit(127); /* execvp only returns on failure */
    }

    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        int status;
        waitpid(pid, &status, 0);
        strncpy(g_status_msg, " Error executing command", sizeof(g_status_msg)-1);
        g_status_msg[sizeof(g_status_msg)-1] = '\0';
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) && g_item_count < MAX_ITEMS) {
        strip_ansi(line);
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
            len--;
        }
        
        if (len == 0 || strstr(line, "---") || strstr(line, "===")) {
            continue;
        }

        if (strstr(line, "BotPkg") && strstr(line, "Packages")) {
            continue;
        }

        strncpy(g_items[g_item_count], line, sizeof(g_items[g_item_count])-1);
        g_items[g_item_count][sizeof(g_items[g_item_count])-1] = '\0';
        g_item_count++;
    }
    fclose(fp); /* also closes pipefd[0] */

    int status;
    waitpid(pid, &status, 0);
    snprintf(g_status_msg, sizeof(g_status_msg), " Done. %d items loaded.", g_item_count);
}

static void render_gui(void)
{
    bot_theme_t *th = bot_theme_get();
    bot_canvas_clear(g_canvas, th->bg);

    bot_draw_window_frame(g_canvas, 0, 0, g_width, g_height, "BotPkg GUI", TITLE_H);

    bot_draw_text(g_canvas, 20, TITLE_H + 15, "Search Package:", th->fg, 1);
    bot_draw_textbox(g_canvas, 20, TITLE_H + 30, 260, 24, g_search_query, g_search_active ? g_search_cursor : -1);

    bot_draw_button(g_canvas, 290, TITLE_H + 30, 90, 24, "SEARCH", BOT_BTN_NORMAL);
    bot_draw_button(g_canvas, 390, TITLE_H + 30, 190, 24, "LIST INSTALLED", BOT_BTN_NORMAL);

    int list_y = TITLE_H + 65;
    int list_h = g_height - STATUS_HEIGHT - list_y - 45;
    int list_w = g_width - 40;
    
    bot_draw_panel(g_canvas, 20, list_y, list_w, list_h);
    bot_canvas_draw_rect(g_canvas, 20, list_y, list_w, list_h, th->panel_border);

    int visible_rows = list_h / BOT_CHAR_H;
    for (int i = 0; i < visible_rows && i < g_item_count; i++) {
        int item_y = list_y + 4 + i * BOT_CHAR_H;
        if (i == g_selected_idx) {
            bot_canvas_fill_rect(g_canvas, 22, item_y, list_w - 4, BOT_CHAR_H, th->highlight);
        }
        bot_draw_text(g_canvas, 26, item_y + 1, g_items[i], th->fg, 1);
    }

    int action_y = g_height - STATUS_HEIGHT - 35;
    bot_draw_separator(g_canvas, 0, action_y - 5, g_width);
    bot_draw_button(g_canvas, 20, action_y, 160, 24, "INSTALL SELECTED", BOT_BTN_NORMAL);
    bot_draw_button(g_canvas, 200, action_y, 160, 24, "REMOVE SELECTED", BOT_BTN_NORMAL);
    bot_draw_button(g_canvas, 420, action_y, 160, 24, "Close", BOT_BTN_NORMAL);

    bot_draw_separator(g_canvas, 0, g_height - STATUS_HEIGHT - 1, g_width);
    bot_draw_status_bar(g_canvas, 0, g_height - STATUS_HEIGHT, g_width, STATUS_HEIGHT, g_status_msg);

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

    if (bot_button_hit(20, TITLE_H + 30, 260, 24, mx, my)) {
        g_search_active = 1;
        g_needs_redraw = 1;
        return;
    }
    g_search_active = 0;

    if (bot_button_hit(290, TITLE_H + 30, 90, 24, mx, my)) {
        char *argv[] = { "botpkg", "search", g_search_query, NULL };
        run_botpkg_cmd(argv);
        g_needs_redraw = 1;
        return;
    }

    if (bot_button_hit(390, TITLE_H + 30, 190, 24, mx, my)) {
        char *argv[] = { "botpkg", "list", NULL };
        run_botpkg_cmd(argv);
        g_needs_redraw = 1;
        return;
    }

    int list_y = TITLE_H + 65;
    int list_h = g_height - STATUS_HEIGHT - list_y - 45;
    int list_w = g_width - 40;
    if (mx >= 20 && mx < 20 + list_w && my >= list_y && my < list_y + list_h) {
        int clicked_row = (my - list_y - 4) / BOT_CHAR_H;
        if (clicked_row >= 0 && clicked_row < g_item_count) {
            g_selected_idx = clicked_row;
            g_needs_redraw = 1;
        }
        return;
    }

    int action_y = g_height - STATUS_HEIGHT - 35;
    if (bot_button_hit(20, action_y, 160, 24, mx, my)) {
        if (g_selected_idx >= 0) {
            char pkg_name[64] = "";
            sscanf(g_items[g_selected_idx], "%63s", pkg_name);
            if (strlen(pkg_name) > 0) {
                char *argv[] = { "botpkg", "install", pkg_name, NULL };
                run_botpkg_cmd(argv);
            }
        }
        g_needs_redraw = 1;
    }
    else if (bot_button_hit(200, action_y, 160, 24, mx, my)) {
        if (g_selected_idx >= 0) {
            char pkg_name[64] = "";
            sscanf(g_items[g_selected_idx], "%63s", pkg_name);
            if (strlen(pkg_name) > 0) {
                char *argv[] = { "botpkg", "remove", pkg_name, NULL };
                run_botpkg_cmd(argv);
            }
        }
        g_needs_redraw = 1;
    }
    else if (bot_button_hit(420, action_y, 160, 24, mx, my)) {
        bot_event_quit();
    }
}

static void on_key_down(const bot_event_t *ev, void *data)
{
    (void)data;
    int key = ev->key.keycode;

    if (key == BOT_KEY_ESCAPE) {
        bot_event_quit();
        return;
    }

    if (g_search_active) {
        if (key == BOT_KEY_ENTER || key == '\n' || key == '\r') {
            g_search_active = 0;
            char *argv[] = { "botpkg", "search", g_search_query, NULL };
            run_botpkg_cmd(argv);
            g_needs_redraw = 1;
            return;
        }
        if (key == BOT_KEY_BACKSPACE || key == 127) {
            int len = (int)strlen(g_search_query);
            if (len > 0) {
                g_search_query[len - 1] = '\0';
                g_search_cursor = len - 1;
                g_needs_redraw = 1;
            }
            return;
        }
        if (key >= 32 && key <= 126) {
            int len = (int)strlen(g_search_query);
            if (len < (int)sizeof(g_search_query) - 1) {
                g_search_query[len] = (char)key;
                g_search_query[len + 1] = '\0';
                g_search_cursor = len + 1;
                g_needs_redraw = 1;
            }
        }
    }
}

static void on_resize(const bot_event_t *ev, void *data)
{
    (void)data;
    g_width = ev->resize.width;
    g_height = ev->resize.height;
    /* Keep window.c's own framebuffer in sync with the new size —
     * otherwise the blit at the end of render_gui() overflows it as
     * soon as the window manager resizes this window. */
    bot_window_resize(g_window, g_width, g_height);
    bot_canvas_destroy(g_canvas);
    g_canvas = bot_canvas_create(g_width, g_height);
    g_needs_redraw = 1;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    bot_log_init(NULL, BOT_LOG_INFO);

    if (bot_ui_init() != 0) return 1;

    g_window = bot_window_create("BotPkg GUI", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (!g_window || !g_canvas) return 1;

    char *init_argv[] = { "botpkg", "list", NULL };
    run_botpkg_cmd(init_argv);

    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_KEY_DOWN, on_key_down, NULL);
    bot_event_on(BOT_EVENT_RESIZE, on_resize, NULL);

    bot_window_show(g_window);
    render_gui();

    bot_event_t event;
    while (!bot_window_should_close(g_window)) {
        while (bot_event_poll(&event))
            if (event.type == BOT_EVENT_CLOSE) goto cleanup;
        if (g_needs_redraw) render_gui();
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
