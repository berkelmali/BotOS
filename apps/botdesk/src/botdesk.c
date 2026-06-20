/* ============================================================
 * BotOS Core — BotDesk Desktop Shell (Production)
 * ============================================================
 * File:    botdesk.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 4 — Production desktop environment.
 * Refactored to use the bot_widget toolkit.
 *
 * Layout (1024×768):
 *   ┌─────────────────────────────────────────┐
 *   │              Desktop Area               │
 *   │          (gradient + icons)             │
 *   ├─────────┬───────────────────────┬───────┤ y=736
 *   │ ◉ Menu  │     Window List      │ Clock │ Taskbar
 *   └─────────┴───────────────────────┴───────┘ y=768
 * ============================================================ */

#include "bot_ui.h"
#include "bot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ── Layout Constants ────────────────────────────────────── */

#define DESK_WIDTH       1024
#define DESK_HEIGHT      768
#define TASKBAR_HEIGHT   32
#define TASKBAR_Y        (DESK_HEIGHT - TASKBAR_HEIGHT)
#define MENU_BTN_WIDTH   80
#define MENU_BTN_HEIGHT  (TASKBAR_HEIGHT - 4)
#define CLOCK_WIDTH      80
#define ICON_SIZE        48
#define ICON_GRID_X      40
#define ICON_GRID_Y      40
#define ICON_SPACING     80

/* ── Desktop State Forward Declarations ──────────────────── */
static bot_window_t *g_window      = NULL;
static bot_canvas_t *g_canvas      = NULL;
static int           g_needs_redraw = 1;

/* ── Wallpaper Colors ────────────────────────────────────── */

#define COL_BG_TOP       BOT_RGBA(12,  15,  30,  255)
#define COL_BG_BOT       BOT_RGBA(25,  35,  65,  255)

static double get_cpu_usage(void)
{
    static long double a[4], b[4];
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;
    
    char cpu[10];
    if (fscanf(fp, "%s %Lf %Lf %Lf %Lf", cpu, &b[0], &b[1], &b[2], &b[3]) != 5) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);
    
    long double load = (b[0] + b[1] + b[2]) - (a[0] + a[1] + a[2]);
    long double total = (b[0] + b[1] + b[2] + b[3]) - (a[0] + a[1] + a[2] + a[3]);
    
    a[0] = b[0]; a[1] = b[1]; a[2] = b[2]; a[3] = b[3];
    
    if (total == 0) return 0.0;
    double usage = (double)(load / total) * 100.0;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return usage;
}

static double get_ram_usage(void)
{
    long total_mem = 0, free_mem = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0.0;
    
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        long val;
        if (sscanf(line, "MemTotal: %ld kB", &val) == 1) total_mem = val;
        else if (sscanf(line, "MemFree: %ld kB", &val) == 1) free_mem = val;
    }
    fclose(fp);
    
    if (total_mem == 0) return 0.0;
    long used_mem = total_mem - free_mem;
    double usage = (double)used_mem / (double)total_mem * 100.0;
    return usage;
}

static void draw_system_widget(void)
{
    int wx = 800;
    int wy = 20;
    int ww = 204;
    int wh = 80;

    bot_theme_t *th = bot_theme_get();

    bot_draw_panel(g_canvas, wx, wy, ww, wh);
    bot_canvas_draw_rect(g_canvas, wx, wy, ww, wh, th->panel_border);

    bot_draw_text(g_canvas, wx + 8, wy + 6, "SYSTEM MONITOR", th->fg, 1);
    bot_draw_separator(g_canvas, wx, wy + 16, ww);

    double cpu = get_cpu_usage();
    double ram = get_ram_usage();

    char cpu_txt[32];
    snprintf(cpu_txt, sizeof(cpu_txt), "CPU: %3.0f%%", cpu);
    bot_draw_text(g_canvas, wx + 8, wy + 24, cpu_txt, th->fg, 1);
    
    int bar_w = 120;
    int bar_h = 8;
    int bar_x = wx + 70;
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 24, bar_w, bar_h, th->btn_normal);
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 24, (int)(bar_w * cpu / 100.0), bar_h, th->accent);

    char ram_txt[32];
    snprintf(ram_txt, sizeof(ram_txt), "RAM: %3.0f%%", ram);
    bot_draw_text(g_canvas, wx + 8, wy + 42, ram_txt, th->fg, 1);
    
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 42, bar_w, bar_h, th->btn_normal);
    bot_canvas_fill_rect(g_canvas, bar_x, wy + 42, (int)(bar_w * ram / 100.0), bar_h, th->accent);
    
    double uptime_secs = 0.0;
    FILE *uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file) {
        if (fscanf(uptime_file, "%lf", &uptime_secs) != 1) uptime_secs = 0.0;
        fclose(uptime_file);
    }
    int uptime_h = (int)(uptime_secs / 3600);
    int uptime_m = (int)((uptime_secs - uptime_h * 3600) / 60);
    char uptime_txt[64];
    snprintf(uptime_txt, sizeof(uptime_txt), "Uptime: %dh %dm", uptime_h, uptime_m);
    bot_draw_text(g_canvas, wx + 8, wy + 62, uptime_txt, th->fg_dim, 1);
}

static void draw_wallpaper(void)
{
    bot_theme_t *th = bot_theme_get();
    if (th->bg == 0xFF000000) {
        bot_canvas_clear(g_canvas, 0xFF000000);
    } else if (th->bg == 0xFFF0F0F0) {
        bot_draw_gradient_v(g_canvas, 0, 0, DESK_WIDTH, TASKBAR_Y,
                            BOT_RGBA(230, 235, 250, 255), BOT_RGBA(190, 200, 230, 255));
    } else {
        bot_draw_gradient_v(g_canvas, 0, 0, DESK_WIDTH, TASKBAR_Y,
                            COL_BG_TOP, COL_BG_BOT);
    }
}

/* ── Desktop Icon ────────────────────────────────────────── */

typedef struct {
    const char *label;
    int         x, y;
    bot_color_t icon_color;
} desktop_icon_t;

static desktop_icon_t g_icons[] = {
    { "Shell",   0, 0, BOT_RGBA(80,  200, 120, 255) },
    { "Paint",   0, 0, BOT_RGBA(255, 120, 80,  255) },
    { "Editor",  0, 0, BOT_RGBA(80,  160, 255, 255) },
    { "Files",   0, 0, BOT_RGBA(255, 200, 60,  255) },
    { "BotPkg",  0, 0, BOT_RGBA(180, 100, 255, 255) },
    { "Settings",0, 0, BOT_RGBA(120, 220, 220, 255) },
    { "Info",    0, 0, BOT_RGBA(255, 105, 180, 255) },
};

#define ICON_COUNT  (int)(sizeof(g_icons) / sizeof(g_icons[0]))

/* ── Desktop State ───────────────────────────────────────── */

static int           g_menu_hover  = 0;
static int           g_desktop_mouse_x = 0;
static int           g_desktop_mouse_y = 0;
static uint32_t      g_last_click_time = 0;
static int           g_last_clicked_icon = -1;

static int           g_show_alert = 0;
static char          g_alert_message[128] = "";

static uint32_t get_ticks_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── Layout ──────────────────────────────────────────────── */

static void layout_icons(void)
{
    for (int i = 0; i < ICON_COUNT; i++) {
        g_icons[i].x = ICON_GRID_X + (i % 2) * (ICON_SIZE + ICON_SPACING);
        g_icons[i].y = ICON_GRID_Y + (i / 2) * (ICON_SIZE + ICON_SPACING);
    }
}

/* ── Rendering ───────────────────────────────────────────── */

static void render_desktop(void)
{
    bot_theme_load();
    draw_wallpaper();

    /* Desktop icons via widget toolkit */
    for (int i = 0; i < ICON_COUNT; i++) {
        int x1 = g_icons[i].x;
        int y1 = g_icons[i].y;
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20;

        int hovered = (g_desktop_mouse_x >= x1 && g_desktop_mouse_x < x2 &&
                       g_desktop_mouse_y >= y1 && g_desktop_mouse_y < y2);

        /* Draw a subtle background highlight if hovered */
        if (hovered) {
            bot_canvas_fill_rect(g_canvas, x1 - 6, y1 - 6, ICON_SIZE + 12, ICON_SIZE + 28,
                                 BOT_RGBA(255, 255, 255, 30));
        }

        bot_draw_icon(g_canvas, g_icons[i].x, g_icons[i].y,
                      ICON_SIZE, g_icons[i].icon_color, g_icons[i].label);
    }

    /* Draw System Monitor Widget */
    draw_system_widget();

    /* Taskbar panel */
    bot_draw_panel(g_canvas, 0, TASKBAR_Y, DESK_WIDTH, TASKBAR_HEIGHT);
    bot_draw_separator(g_canvas, 0, TASKBAR_Y, DESK_WIDTH);

    /* Menu button */
    bot_btn_state_t menu_state = g_menu_hover ? BOT_BTN_HOVER : BOT_BTN_NORMAL;
    bot_draw_button(g_canvas, 2, TASKBAR_Y + 2, MENU_BTN_WIDTH, MENU_BTN_HEIGHT,
                    "MENU", menu_state);

    /* BotOS logo circle on menu button */
    bot_theme_t *th = bot_theme_get();
    bot_canvas_draw_circle(g_canvas, 16, TASKBAR_Y + TASKBAR_HEIGHT / 2,
                           8, th->accent);

    /* Clock area */
    int clock_x = DESK_WIDTH - CLOCK_WIDTH;
    bot_draw_button(g_canvas, clock_x, TASKBAR_Y + 2,
                    CLOCK_WIDTH - 2, MENU_BTN_HEIGHT, NULL, BOT_BTN_NORMAL);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm->tm_hour, tm->tm_min);
    bot_draw_text(g_canvas, clock_x + 14, TASKBAR_Y + 10,
                  time_str, th->fg, 2);

    /* Window list label */
    bot_draw_text(g_canvas, MENU_BTN_WIDTH + 16, TASKBAR_Y + 13,
                  "BOTDESK", th->fg_dim, 1);

    /* Draw alert modal if active */
    if (g_show_alert) {
        int box_w = 400;
        int box_h = 120;
        int box_x = (DESK_WIDTH - box_w) / 2;
        int box_y = (DESK_HEIGHT - box_h) / 2;

        /* Dim the screen background */
        bot_canvas_fill_rect(g_canvas, 0, 0, DESK_WIDTH, TASKBAR_Y, BOT_RGBA(0, 0, 0, 100));

        /* Draw panel and decoration */
        bot_draw_panel(g_canvas, box_x, box_y, box_w, box_h);
        bot_draw_window_frame(g_canvas, box_x, box_y, box_w, box_h, "Bilgi", 24);

        /* Center and draw text message */
        int text_w = bot_measure_text(g_alert_message, 1);
        int tx = box_x + (box_w - text_w) / 2;
        bot_draw_text(g_canvas, tx, box_y + 48, g_alert_message, th->fg, 1);

        /* Draw OK button */
        int btn_w = 60;
        int btn_h = 24;
        int btn_x = box_x + (box_w - btn_w) / 2;
        int btn_y = box_y + box_h - 36;
        
        bot_draw_button(g_canvas, btn_x, btn_y, btn_w, btn_h, "Tamam", BOT_BTN_NORMAL);
    }

    /* Blit canvas → framebuffer */
    uint32_t *fb = bot_window_get_framebuffer(g_window);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    if (fb && px)
        memcpy(fb, px, (size_t)(DESK_WIDTH * DESK_HEIGHT) * sizeof(uint32_t));
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

/* ── Event Handlers ──────────────────────────────────────── */

static void launch_app(const char *path, char *const argv[])
{
    /* 1. Release UI resources */
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();

    /* 2. Fork and execute */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child */
        execvp(path, argv);
        exit(127);
    } else if (pid > 0) {
        /* Parent: wait for child to finish */
        int status;
        waitpid(pid, &status, 0);
    }

    /* 3. Re-acquire UI resources */
    bot_ui_init();
    g_window = bot_window_create("BotDesk", DESK_WIDTH, DESK_HEIGHT);
    g_canvas = bot_canvas_create(DESK_WIDTH, DESK_HEIGHT);
    if (g_window) bot_window_show(g_window);
    g_needs_redraw = 1;
}

static void on_mouse_down(const bot_event_t *event, void *data)
{
    (void)data;
    int mx = event->mouse.x;
    int my = event->mouse.y;
    uint32_t now = get_ticks_ms();

    if (g_show_alert) {
        int box_w = 400;
        int box_h = 120;
        int box_x = (DESK_WIDTH - box_w) / 2;
        int box_y = (DESK_HEIGHT - box_h) / 2;
        int btn_w = 60;
        int btn_h = 24;
        int btn_x = box_x + (box_w - btn_w) / 2;
        int btn_y = box_y + box_h - 36;

        if (mx >= btn_x && mx < btn_x + btn_w && my >= btn_y && my < btn_y + btn_h) {
            g_show_alert = 0;
            g_needs_redraw = 1;
        }
        return;
    }

    /* Check icons */
    for (int i = 0; i < ICON_COUNT; i++) {
        int x1 = g_icons[i].x;
        int y1 = g_icons[i].y;
        int x2 = x1 + ICON_SIZE;
        int y2 = y1 + ICON_SIZE + 20; /* Expands to cover the text label */

        if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
            /* Check if it is a double click on the same icon */
            if (g_last_clicked_icon == i && (now - g_last_click_time) < 500) {
                /* Double click detected! Launch app */
                g_last_clicked_icon = -1; /* Reset */
                if (strcmp(g_icons[i].label, "Shell") == 0) {
                    char *argv[] = { "/usr/bin/botterm", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Paint") == 0) {
                    char *argv[] = { "/usr/bin/botpaint", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Editor") == 0) {
                    char *argv[] = { "/usr/bin/editor", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Files") == 0) {
                    char *argv[] = { "/usr/bin/botfiles", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "BotPkg") == 0) {
                    char *argv[] = { "/usr/bin/botpkg_gui", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Settings") == 0) {
                    char *argv[] = { "/usr/bin/botsettings", NULL };
                    launch_app(argv[0], argv);
                } else if (strcmp(g_icons[i].label, "Info") == 0) {
                    char *argv[] = { "/usr/bin/botinfo", NULL };
                    launch_app(argv[0], argv);
                } else {
                    snprintf(g_alert_message, sizeof(g_alert_message), "%s uygulamasi yakinda eklenecektir!", g_icons[i].label);
                    g_show_alert = 1;
                    g_needs_redraw = 1;
                }
            } else {
                g_last_clicked_icon = i;
                g_last_click_time = now;
            }
            break;
        }
    }
}

static void on_mouse_move(const bot_event_t *event, void *data)
{
    (void)data;
    g_desktop_mouse_x = event->mouse.x;
    g_desktop_mouse_y = event->mouse.y;
    g_menu_hover = bot_button_hit(2, TASKBAR_Y + 2, MENU_BTN_WIDTH,
                                  MENU_BTN_HEIGHT, g_desktop_mouse_x, g_desktop_mouse_y);
    g_needs_redraw = 1;
}

static void on_key_down(const bot_event_t *event, void *data)
{
    (void)data;
    if ((event->key.keycode == 'q' && (event->key.modifiers & 2)) ||
        event->key.keycode == BOT_KEY_ESCAPE) {
        bot_event_quit();
    }
}

static void on_resize(const bot_event_t *event, void *data)
{
    (void)event; (void)data;
    g_needs_redraw = 1;
}

/* ── Main ────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    bot_log_init(NULL, BOT_LOG_INFO);
    BOT_LOG_INFO("BotDesk starting...");

    if (bot_ui_init() != 0) {
        BOT_LOG_FATAL("Failed to initialize UI subsystem");
        return 1;
    }

    g_window = bot_window_create("BotDesk", DESK_WIDTH, DESK_HEIGHT);
    g_canvas = bot_canvas_create(DESK_WIDTH, DESK_HEIGHT);
    if (!g_window || !g_canvas) {
        BOT_LOG_FATAL("Failed to create desktop window/canvas");
        bot_ui_shutdown();
        return 1;
    }

    layout_icons();

    bot_event_on(BOT_EVENT_MOUSE_MOVE, on_mouse_move, NULL);
    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_KEY_DOWN,   on_key_down,   NULL);
    bot_event_on(BOT_EVENT_RESIZE,     on_resize,     NULL);

    bot_window_show(g_window);
    render_desktop();

    bot_event_t event;
    uint32_t last_update = 0;
    while (!bot_window_should_close(g_window)) {
        uint32_t now = get_ticks_ms();
        if (now - last_update >= 2000) {
            g_needs_redraw = 1;
            last_update = now;
        }

        while (bot_event_poll(&event)) {
            if (event.type == BOT_EVENT_CLOSE) goto cleanup;
        }

        if (g_needs_redraw) {
            render_desktop();
        }

        usleep(50000);
    }

cleanup:
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    bot_log_shutdown();
    return 0;
}
