/* ============================================================
 * BotOS Core — Graphical Terminal Emulator (Production)
 * ============================================================
 * File:    botterm.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Terminal emulator running botshell in a PTY.
 * ============================================================ */

#define _XOPEN_SOURCE 600
#include "bot_ui.h"
#include "bot.h"
#include "bot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>

#define TERM_COLS      80
#define TERM_ROWS      25
#define CHAR_W         12  /* Scale 2 */
#define CHAR_H         16  /* Scale 2 */

#define TITLE_H        24
#define STATUS_HEIGHT  22
#define PADDING        8

#define WIN_WIDTH      (TERM_COLS * CHAR_W + PADDING * 2)
#define WIN_HEIGHT     (TERM_ROWS * CHAR_H + PADDING * 2 + TITLE_H + STATUS_HEIGHT)

static int g_width = WIN_WIDTH;
static int g_height = WIN_HEIGHT;

#define SCROLLBACK_MAX 1000

static bot_color_t g_ansi_colors[16] = {
    BOT_RGBA(  0,   0,   0, 255), /* 0: Black */
    BOT_RGBA(205,   0,   0, 255), /* 1: Red */
    BOT_RGBA(  0, 205,   0, 255), /* 2: Green */
    BOT_RGBA(205, 205,   0, 255), /* 3: Yellow */
    BOT_RGBA( 59, 120, 255, 255), /* 4: Blue */
    BOT_RGBA(205,   0, 205, 255), /* 5: Magenta */
    BOT_RGBA(  0, 205, 205, 255), /* 6: Cyan */
    BOT_RGBA(229, 229, 229, 255), /* 7: White */
    BOT_RGBA(127, 127, 127, 255), /* 8: Bright Black (Grey) */
    BOT_RGBA(255,  92,  92, 255), /* 9: Bright Red */
    BOT_RGBA( 92, 255,  92, 255), /* 10: Bright Green */
    BOT_RGBA(255, 255,  92, 255), /* 11: Bright Yellow */
    BOT_RGBA( 92,  92, 255, 255), /* 12: Bright Blue */
    BOT_RGBA(255,  92, 255, 255), /* 13: Bright Magenta */
    BOT_RGBA( 92, 255, 255, 255), /* 14: Bright Cyan */
    BOT_RGBA(255, 255, 255, 255)  /* 15: Bright White */
};

static char        g_grid[TERM_ROWS][TERM_COLS];
static bot_color_t g_color_grid[TERM_ROWS][TERM_COLS];
static int         g_cursor_row = 0;
static int         g_cursor_col = 0;
static int         g_esc_state = 0;
static char        g_esc_buf[32];
static int         g_esc_buf_len = 0;
static bot_color_t g_current_color = BOT_RGBA(240, 240, 240, 255);

static char        g_scrollback[SCROLLBACK_MAX][TERM_COLS];
static bot_color_t g_scrollback_colors[SCROLLBACK_MAX][TERM_COLS];
static int         g_scrollback_count = 0;
static int         g_scroll_offset = 0;

static bot_window_t *g_window = NULL;
static bot_canvas_t *g_canvas = NULL;
static int           g_needs_redraw = 1;
static int           g_master_fd = -1;
static pid_t         g_shell_pid = -1;

static void scroll_grid(void)
{
    /* Save top line to scrollback */
    if (g_scrollback_count < SCROLLBACK_MAX) {
        memcpy(g_scrollback[g_scrollback_count], g_grid[0], TERM_COLS);
        memcpy(g_scrollback_colors[g_scrollback_count], g_color_grid[0], TERM_COLS * sizeof(bot_color_t));
        g_scrollback_count++;
    } else {
        memmove(g_scrollback[0], g_scrollback[1], (SCROLLBACK_MAX - 1) * TERM_COLS);
        memmove(g_scrollback_colors[0], g_scrollback_colors[1], (SCROLLBACK_MAX - 1) * TERM_COLS * sizeof(bot_color_t));
        memcpy(g_scrollback[SCROLLBACK_MAX - 1], g_grid[0], TERM_COLS);
        memcpy(g_scrollback_colors[SCROLLBACK_MAX - 1], g_color_grid[0], TERM_COLS * sizeof(bot_color_t));
    }

    memmove(g_grid[0], g_grid[1], (TERM_ROWS - 1) * TERM_COLS);
    memset(g_grid[TERM_ROWS - 1], ' ', TERM_COLS);

    memmove(g_color_grid[0], g_color_grid[1], (TERM_ROWS - 1) * TERM_COLS * sizeof(bot_color_t));
    for (int c = 0; c < TERM_COLS; c++) {
        g_color_grid[TERM_ROWS - 1][c] = g_current_color;
    }
}

static void write_char_to_grid(char c)
{
    if (g_esc_state == 0) {
        if (c == '\033') {
            g_esc_state = 1;
            g_esc_buf_len = 0;
            g_esc_buf[0] = '\0';
        } else if (c == '\n') {
            g_cursor_row++;
            if (g_cursor_row >= TERM_ROWS) {
                scroll_grid();
                g_cursor_row = TERM_ROWS - 1;
            }
        } else if (c == '\r') {
            g_cursor_col = 0;
        } else if (c == '\b' || c == 127) {
            if (g_cursor_col > 0) g_cursor_col--;
        } else if (c == '\t') {
            g_cursor_col = (g_cursor_col + 8) & ~7;
            if (g_cursor_col >= TERM_COLS) g_cursor_col = TERM_COLS - 1;
        } else if (c >= 32 && c <= 126) {
            g_grid[g_cursor_row][g_cursor_col] = c;
            g_color_grid[g_cursor_row][g_cursor_col] = g_current_color;
            g_cursor_col++;
            if (g_cursor_col >= TERM_COLS) {
                g_cursor_col = 0;
                g_cursor_row++;
                if (g_cursor_row >= TERM_ROWS) {
                    scroll_grid();
                    g_cursor_row = TERM_ROWS - 1;
                }
            }
        }
    } else if (g_esc_state == 1) {
        if (c == '[') {
            g_esc_state = 2;
        } else {
            g_esc_state = 0;
        }
    } else if (g_esc_state == 2) {
        if ((c >= '0' && c <= '9') || c == ';') {
            if (g_esc_buf_len < 31) {
                g_esc_buf[g_esc_buf_len++] = c;
                g_esc_buf[g_esc_buf_len] = '\0';
            }
        }
        
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            if (c == 'm') {
                /* Parse SGR color commands */
                char *token = strtok(g_esc_buf, ";");
                while (token) {
                    int val = atoi(token);
                    if (val == 0) {
                        g_current_color = BOT_RGBA(240, 240, 240, 255);
                    } else if (val >= 30 && val <= 37) {
                        g_current_color = g_ansi_colors[val - 30];
                    } else if (val >= 90 && val <= 97) {
                        g_current_color = g_ansi_colors[val - 90 + 8];
                    }
                    token = strtok(NULL, ";");
                }
            } else if (c == 'J') {
                /* Clear screen */
                memset(g_grid, ' ', sizeof(g_grid));
                for (int r = 0; r < TERM_ROWS; r++) {
                    for (int col = 0; col < TERM_COLS; col++) {
                        g_color_grid[r][col] = BOT_RGBA(240, 240, 240, 255);
                    }
                }
                g_cursor_row = 0;
                g_cursor_col = 0;
                g_scroll_offset = 0;
            }
            g_esc_state = 0;
        }
    }
}

static void render_term(void)
{
    bot_theme_t *th = bot_theme_get();

    /* Clear and draw frame */
    bot_draw_window_frame(g_canvas, 0, 0, g_width, g_height, "BotOS Terminal", TITLE_H);

    /* Draw grid characters centered */
    int start_y = TITLE_H + (g_height - TITLE_H - STATUS_HEIGHT - TERM_ROWS * CHAR_H) / 2;
    int start_x = (g_width - TERM_COLS * CHAR_W) / 2;
    if (start_y < TITLE_H + PADDING) start_y = TITLE_H + PADDING;
    if (start_x < PADDING) start_x = PADDING;

    for (int r = 0; r < TERM_ROWS; r++) {
        int py = start_y + r * CHAR_H;
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = ' ';
            bot_color_t color = g_current_color;

            if (g_scroll_offset > 0) {
                int virtual_row = g_scrollback_count - g_scroll_offset + r;
                if (virtual_row >= 0) {
                    if (virtual_row < g_scrollback_count) {
                        ch = g_scrollback[virtual_row][c];
                        color = g_scrollback_colors[virtual_row][c];
                    } else {
                        ch = g_grid[virtual_row - g_scrollback_count][c];
                        color = g_color_grid[virtual_row - g_scrollback_count][c];
                    }
                }
            } else {
                ch = g_grid[r][c];
                color = g_color_grid[r][c];
            }

            if (ch != ' ' && ch != '\0') {
                bot_draw_char(g_canvas, start_x + c * CHAR_W, py, ch, color, 2);
            }
        }
    }

    /* Draw cursor */
    int vis_cursor_row = g_cursor_row + g_scroll_offset;
    if (vis_cursor_row >= 0 && vis_cursor_row < TERM_ROWS) {
        int cursor_px = start_x + g_cursor_col * CHAR_W;
        int cursor_py = start_y + vis_cursor_row * CHAR_H;
        bot_canvas_fill_rect(g_canvas, cursor_px, cursor_py, CHAR_W, CHAR_H, th->cursor);
    }

    /* Status bar */
    char status_msg[128];
    if (g_scroll_offset > 0) {
        snprintf(status_msg, sizeof(status_msg), " botshell running  [SCROLLED UP: %d]  [ESC: Exit]", g_scroll_offset);
    } else {
        snprintf(status_msg, sizeof(status_msg), " botshell running   [ESC: Exit]");
    }
    bot_draw_status_bar(g_canvas, 0, g_height - STATUS_HEIGHT, g_width, STATUS_HEIGHT, status_msg);

    /* Blit to window */
    uint32_t *fb = bot_window_get_framebuffer(g_window);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    if (fb && px) {
        memcpy(fb, px, (size_t)(g_width * g_height) * sizeof(uint32_t));
    }
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

static void on_key(const bot_event_t *ev, void *data)
{
    int key = ev->key.keycode;
    if (key == BOT_KEY_ESCAPE) {
        bot_event_quit();
        return;
    }

    g_scroll_offset = 0; /* Reset scroll to bottom on keyboard input */

    char c = 0;
    if (key == BOT_KEY_ENTER) {
        c = '\n';
    } else if (key == BOT_KEY_BACKSPACE) {
        c = '\177';
    } else if (key == BOT_KEY_TAB) {
        c = '\t';
    } else if (key >= 32 && key < 127) {
        c = (char)key;
    }

    if (c != 0 && g_master_fd >= 0) {
        ssize_t written = write(g_master_fd, &c, 1);
        (void)written;
    }
}

static void on_scroll(const bot_event_t *ev, void *data)
{
    (void)data;
    int dy = ev->scroll.dy;
    if (dy > 0) { /* Scroll up */
        if (g_scroll_offset < g_scrollback_count) {
            g_scroll_offset++;
            g_needs_redraw = 1;
        }
    } else if (dy < 0) { /* Scroll down */
        if (g_scroll_offset > 0) {
            g_scroll_offset--;
            g_needs_redraw = 1;
        }
    }
}

static void on_resize(const bot_event_t *ev, void *data)
{
    (void)data;
    g_width = ev->resize.width;
    g_height = ev->resize.height;
    /* Keep window.c's own framebuffer in sync with the new size too —
     * only resizing our canvas here left it stale, so the later
     * memcpy(fb, canvas_pixels, new_w*new_h*4) blit would write past
     * the end of the still-old-sized framebuffer the moment anything
     * external (the WM, now) actually resized this window. */
    bot_window_resize(g_window, g_width, g_height);
    bot_canvas_destroy(g_canvas);
    g_canvas = bot_canvas_create(g_width, g_height);
    g_needs_redraw = 1;
}

int main(int argc, char *argv[])
{
    bot_log_init(NULL, BOT_LOG_INFO);
    memset(g_grid, ' ', sizeof(g_grid));
    g_current_color = BOT_RGBA(240, 240, 240, 255);
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            g_color_grid[r][c] = g_current_color;
        }
    }

    /* Initialize PTY */
    g_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (g_master_fd < 0) {
        BOT_LOG_FATAL("Failed to open pseudoterminal");
        return 1;
    }
    grantpt(g_master_fd);
    unlockpt(g_master_fd);

    char *slave_name = ptsname(g_master_fd);
    g_shell_pid = fork();

    if (g_shell_pid == 0) {
        /* Child: redirect stdin/out/err to slave side */
        close(g_master_fd);
        int slave_fd = open(slave_name, O_RDWR);
        dup2(slave_fd, 0);
        dup2(slave_fd, 1);
        dup2(slave_fd, 2);
        close(slave_fd);

        setsid();
        ioctl(0, TIOCSCTTY, 1);

        char *args[] = { "/usr/bin/botshell", NULL };
        execvp(args[0], args);
        exit(1);
    }

    /* Parent: set master to non-blocking */
    fcntl(g_master_fd, F_SETFL, O_NONBLOCK);

    if (bot_ui_init() != 0) {
        close(g_master_fd);
        kill(g_shell_pid, SIGKILL);
        return 1;
    }

    g_window = bot_window_create("BotOS Terminal", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (!g_window || !g_canvas) {
        bot_ui_shutdown();
        close(g_master_fd);
        kill(g_shell_pid, SIGKILL);
        return 1;
    }

    bot_event_on(BOT_EVENT_KEY_DOWN, on_key, NULL);
    bot_event_on(BOT_EVENT_SCROLL,   on_scroll, NULL);
    bot_event_on(BOT_EVENT_RESIZE,   on_resize, NULL);
    bot_window_show(g_window);
    render_term();

    bot_event_t event;
    while (!bot_window_should_close(g_window)) {
        /* Read any pending PTY output */
        char buf[256];
        ssize_t n;
        int read_any = 0;
        while ((n = read(g_master_fd, buf, sizeof(buf))) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                write_char_to_grid(buf[i]);
            }
            read_any = 1;
        }

        if (read_any) g_needs_redraw = 1;

        while (bot_event_poll(&event)) {
            if (event.type == BOT_EVENT_CLOSE) goto done;
        }

        if (g_needs_redraw) render_term();

        bot_event_wait(&event);
        if (event.type == BOT_EVENT_CLOSE) break;
    }

done:
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    close(g_master_fd);
    kill(g_shell_pid, SIGKILL);
    waitpid(g_shell_pid, NULL, 0);
    bot_log_shutdown();
    return 0;
}
