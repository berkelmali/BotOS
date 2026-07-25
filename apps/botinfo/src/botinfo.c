/* ============================================================
 * BotOS Core — BotInfo Introduction App (Production)
 * ============================================================
 * File:    botinfo.c
 * Layer:   L6 — User Interface
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Visually rich introduction and documentation application for
 * BotOS, written in C and leveraging the BotUI toolkit.
 * ============================================================ */

#include "bot_ui.h"
#include "bot_log.h"
#include "bot_widget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE_H          24
#define STATUS_HEIGHT    22
#define SIDEBAR_W        180

static int g_width = 800;
static int g_height = 600;

static bot_window_t *g_window = NULL;
static bot_canvas_t *g_canvas = NULL;
static int g_needs_redraw = 1;

static int g_selected_tab = 0;
static int g_hovered_tab = -1;

static const char *g_tab_labels[] = {
    "Overview",
    "Shell & CLI",
    "RPG Subsystem",
    "GUI Applications",
    "System & SDK"
};

#define TAB_COUNT (int)(sizeof(g_tab_labels) / sizeof(g_tab_labels[0]))

static void draw_text_line(bot_canvas_t *c, int x, int *y, const char *text, bot_color_t color, int scale)
{
    bot_draw_text(c, x, *y, text, color, scale);
    *y += (BOT_CHAR_H * scale) + 6; /* Character height + line spacing */
}

static void render_overview(bot_canvas_t *c, int rx, int ry, bot_theme_t *th)
{
    int y = ry;
    draw_text_line(c, rx, &y, "Welcome to BotOS", th->accent, 2);
    bot_draw_separator(c, rx, y - 2, g_width - rx - 20);
    y += 10;

    draw_text_line(c, rx, &y, "BotOS is a professional, modular operating system built from scratch in C.", th->fg, 1);
    draw_text_line(c, rx, &y, "It integrates a custom microkernel, an execution shell, and a complete", th->fg, 1);
    draw_text_line(c, rx, &y, "graphical window manager stack.", th->fg, 1);
    y += 10;

    draw_text_line(c, rx, &y, "Key Design Goals:", th->accent, 1);
    draw_text_line(c, rx + 10, &y, "- Visual Excellence: Curved window borders, premium themes, clean fonts.", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "- Agent Friendly: Streamlined APIs for automation bots and terminal scripts.", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "- Portability: Fully UEFI bootable disk packaged via Buildroot compiler.", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "- Multitasking UI: Run multiple graphic apps like Paint, Editor, and Files.", th->fg, 1);
    y += 10;

    draw_text_line(c, rx, &y, "Latest System Updates (v0.5.0):", th->accent, 1);
    draw_text_line(c, rx + 10, &y, "- Shell: Added 'rpg' subsystem ('hero', 'mining', 'shop', 'pet').", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "- Calc & Paint: Added robust fixes and improvements.", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "- Shell: Added 'sl' train, 'tuxsay' & cowsay faces (-f tux/cat/dino).", th->fg, 1);
    y += 10;

    draw_text_line(c, rx, &y, "Interactivity Tips:", th->accent, 1);
    draw_text_line(c, rx + 10, &y, "* Double click on desktop icons to launch user space applications.", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "* Type command 'adventure' in Terminal to play the hardware text game.", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "* Change global color themes under the desktop Settings app.", th->fg, 1);
    draw_text_line(c, rx + 10, &y, "* Double-click '.botimg' files inside Files explorer to edit them.", th->fg, 1);
    y += 20;

    /* A nice colored tip box */
    bot_color_t tip_border = th->panel_border;
    bot_canvas_draw_rect(c, rx, y, g_width - rx - 20, 50, tip_border);
    bot_canvas_fill_rect(c, rx + 1, y + 1, g_width - rx - 22, 48, th->panel);
    int ty = y + 12;
    bot_draw_text(c, rx + 12, ty, "TIP: Double-clicking the top right corner button of any window", th->accent, 1);
    bot_draw_text(c, rx + 12, ty + 12, "     will immediately close and terminate the application.", th->fg_dim, 1);
}

static void render_commands(bot_canvas_t *c, int rx, int ry, bot_theme_t *th)
{
    int y = ry;
    draw_text_line(c, rx, &y, "BotShell & CLI Guide", th->accent, 2);
    bot_draw_separator(c, rx, y - 2, g_width - rx - 20);
    y += 8;

    draw_text_line(c, rx, &y, "BotShell is a highly interactive, POSIX-style shell interpreter featuring:", th->fg, 1);
    y += 4;

    struct {
        const char *feature;
        const char *desc;
    } features[] = {
        { "! <command>", "PyBridge Integration - Run Python statements directly in the shell." },
        { "cmd1 | cmd2", "Pipelining - Chain stdout of first command directly to stdin of second." },
        { "cmd > file",  "Redirection - Write command stdout to file (>), append (>>), or read stdin (<)." },
        { "cmd &",       "Background Jobs - Run asynchronously. Manage via 'jobs', 'fg', and 'bg'." },
        { "calc <expr>", "Simple CLI Calculator - Evaluates arithmetic expressions, e.g. 'calc 5 * 3'." },
        { "dashboard",   "System Monitor - Interactive live-updating CPU/Memory utilization dashboard." },
        { "sl / matrix", "ASCII Entertainment - Steam Locomotive animation or green Matrix code rain." },
        { "cowsay / tux", "Talkative ASCII - Customize speech bubble text. Options: -f tux/cat/dino." },
        { "adventure",   "Text Dungeon RPG - Play the legendary 'Quest of Botty' hardware escape game." }
    };

    int num_feats = (int)(sizeof(features) / sizeof(features[0]));
    for (int i = 0; i < num_feats; i++) {
        bot_draw_text(c, rx + 5, y, features[i].feature, th->accent, 1);
        bot_draw_text(c, rx + 115, y, features[i].desc, th->fg_dim, 1);
        y += BOT_CHAR_H + 5;
    }
}

static void render_rpg(bot_canvas_t *c, int rx, int ry, bot_theme_t *th)
{
    int y = ry;
    draw_text_line(c, rx, &y, "Terminal RPG Subsystem Guide", th->accent, 2);
    bot_draw_separator(c, rx, y - 2, g_width - rx - 20);
    y += 8;

    draw_text_line(c, rx, &y, "You can play a full ASCII RPG right inside the shell! Try these commands:", th->fg, 1);
    y += 4;

    draw_text_line(c, rx, &y, "Core Commands:", th->accent, 1);
    bot_draw_text(c, rx + 10, y, "rpg", th->accent, 1);
    bot_draw_text(c, rx + 125, y, "Main RPG command interface. Displays help and subcommands.", th->fg_dim, 1);
    y += BOT_CHAR_H + 4;
    bot_draw_text(c, rx + 10, y, "hero", th->accent, 1);
    bot_draw_text(c, rx + 125, y, "View your hero's character sheet, active pet, and ASCII equipment.", th->fg_dim, 1);
    y += BOT_CHAR_H + 4;
    bot_draw_text(c, rx + 10, y, "mining", th->accent, 1);
    bot_draw_text(c, rx + 125, y, "Mine for Gold & XP. Higher level and better gear boost yields (15s cd).", th->fg_dim, 1);
    y += BOT_CHAR_H + 8;

    draw_text_line(c, rx, &y, "RPG Gear Store (rpg shop / rpg buy <id>):", th->accent, 1);
    bot_draw_text(c, rx + 10, y, "1. Iron Sword (50g)", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Equips cyan blade. Boosts mining Gold yield by +25%.", th->fg_dim, 1);
    y += BOT_CHAR_H + 3;
    bot_draw_text(c, rx + 10, y, "2. Knight Helmet (75g)", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Equips gold helmet. Boosts Max HP by +30.", th->fg_dim, 1);
    y += BOT_CHAR_H + 3;
    bot_draw_text(c, rx + 10, y, "3. Steel Shield (100g)", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Equips iron shield. Boosts Max Mana by +15.", th->fg_dim, 1);
    y += BOT_CHAR_H + 3;
    bot_draw_text(c, rx + 10, y, "4. Golden Armor (150g)", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Equips gold plate. Boosts Max HP by +50 and Max Mana by +20.", th->fg_dim, 1);
    y += BOT_CHAR_H + 8;

    draw_text_line(c, rx, &y, "Pet Companions & Care (rpg pet):", th->accent, 1);
    bot_draw_text(c, rx + 10, y, "pet adopt <t> <name>", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Adopt slime, cat, or dragon companion to join you.", th->fg_dim, 1);
    y += BOT_CHAR_H + 3;
    bot_draw_text(c, rx + 10, y, "pet feed / play", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Feed (5g, +15 love) or play (10m, +25 love). Pets level up at 100.", th->fg_dim, 1);
    y += BOT_CHAR_H + 8;

    draw_text_line(c, rx, &y, "Skill Upgrades (rpg skills / rpg upgrade <id>):", th->accent, 1);
    bot_draw_text(c, rx + 10, y, "[1] Mining Power", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Increases gold yield from mining quests by +25% per level.", th->fg_dim, 1);
    y += BOT_CHAR_H + 3;
    bot_draw_text(c, rx + 10, y, "[2] Toughness", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Boosts maximum HP capacity by +15 per level.", th->fg_dim, 1);
    y += BOT_CHAR_H + 3;
    bot_draw_text(c, rx + 10, y, "[3] Mana Expansion", th->accent, 1);
    bot_draw_text(c, rx + 165, y, "Boosts maximum Mana capacity by +10 per level.", th->fg_dim, 1);
}

static void render_gui_apps(bot_canvas_t *c, int rx, int ry, bot_theme_t *th)
{
    int y = ry;
    draw_text_line(c, rx, &y, "BotOS Graphical Applications", th->accent, 2);
    bot_draw_separator(c, rx, y - 2, g_width - rx - 20);
    y += 8;

    draw_text_line(c, rx, &y, "Double-click desktop icons to launch these custom graphics utilities:", th->fg, 1);
    y += 4;

    struct {
        const char *name;
        const char *desc;
    } apps[] = {
        { "BotDesk",       "Desktop Shell - Manage multiple overlapping windows. Drag titles to move." },
        { "Paint",         "Pixel Art Editor - Pen, Line, Rect, Circle, Fill. Save/load as binary .botimg." },
        { "Editor",        "Text Editor - Full text loading, editing, saving for code and text files." },
        { "Files",         "File Explorer - Navigation. Double-click .txt to Edit, .botimg to Paint." },
        { "Settings",      "Control Panel - Change color themes, adjust mouse pointer speed." },
        { "BotPkg",        "Package Manager - Visual dashboard to install/uninstall system utilities." },
        { "System Monitor","Live Performance - Graphic widget tracking CPU graph, memory, and uptime." }
    };

    int num_apps = (int)(sizeof(apps) / sizeof(apps[0]));
    for (int i = 0; i < num_apps; i++) {
        bot_draw_text(c, rx + 5, y, apps[i].name, th->accent, 1);
        bot_draw_text(c, rx + 115, y, apps[i].desc, th->fg_dim, 1);
        y += BOT_CHAR_H + 5;
    }
}

static void render_system_sdk(bot_canvas_t *c, int rx, int ry, bot_theme_t *th)
{
    int y = ry;
    draw_text_line(c, rx, &y, "System Architecture & BotUI SDK Guide", th->accent, 2);
    bot_draw_separator(c, rx, y - 2, g_width - rx - 20);
    y += 8;

    int col1_y = y;
    int col2_y = y;

    bot_draw_text(c, rx, col1_y, "Layered Architecture (L1-L6):", th->accent, 1);
    col1_y += BOT_CHAR_H + 6;

    const char *layers[] = {
        "+---------------------------------------+",
        "|  L6: Graphic Apps (Paint, Editor)     |",
        "+---------------------------------------+",
        "|  L5: Desktop Shell & Window Manager   |",
        "+---------------------------------------+",
        "|  L4: Dev Layers (SDK, Parser, Shell)  |",
        "+---------------------------------------+",
        "|  L3: Services (VFS, IPC, Init)        |",
        "+---------------------------------------+",
        "|  L2: Kernel (Scheduler, Drivers, Mem) |",
        "+---------------------------------------+",
        "|  L1: UEFI Bootloader & Hardware       |",
        "+---------------------------------------+"
    };
    int num_layers = (int)(sizeof(layers) / sizeof(layers[0]));
    for (int i = 0; i < num_layers; i++) {
        bot_draw_text(c, rx, col1_y, layers[i], th->fg, 1);
        col1_y += BOT_CHAR_H + 2;
    }

    bot_draw_text(c, rx + 270, col2_y, "BotUI Graphics SDK Toolkit APIs:", th->accent, 1);
    col2_y += BOT_CHAR_H + 6;

    struct {
        const char *sig;
        const char *desc;
    } apis[] = {
        { "bot_window_create()", "Allocates interactive GUI window frame." },
        { "bot_canvas_create()", "Allocates double-buffered pixel canvas." },
        { "bot_draw_text()",     "Draws custom 5x7 vector font text." },
        { "bot_draw_button()",   "Draws button (hover/active states)." },
        { "bot_draw_panel()",    "Draws control panel background block." },
        { "bot_event_on()",      "Registers callbacks for mouse/keyboard." },
        { "bot_canvas_clear()",  "Fills entire canvas buffer with color." }
    };
    int num_apis = (int)(sizeof(apis) / sizeof(apis[0]));
    for (int i = 0; i < num_apis; i++) {
        bot_draw_text(c, rx + 270, col2_y, apis[i].sig, th->accent, 1);
        col2_y += BOT_CHAR_H + 2;
        bot_draw_text(c, rx + 280, col2_y, apis[i].desc, th->fg_dim, 1);
        col2_y += BOT_CHAR_H + 6;
    }
}

static void render_info(void)
{
    bot_theme_t *th = bot_theme_get();
    bot_canvas_clear(g_canvas, th->bg);

    /* Main Window Frame */
    bot_draw_window_frame(g_canvas, 0, 0, g_width, g_height, "System Documentation & Architecture Guide", TITLE_H);

    /* Left Sidebar Panel */
    int side_y = TITLE_H;
    int side_h = g_height - TITLE_H - STATUS_HEIGHT;
    bot_draw_panel(g_canvas, 0, side_y, SIDEBAR_W, side_h);

    /* Draw Tab Buttons inside Sidebar */
    for (int i = 0; i < TAB_COUNT; i++) {
        int ty = side_y + 12 + i * 36;
        int tx = 10;
        int tw = SIDEBAR_W - 20;
        int th_btn = 28;

        bot_btn_state_t state = BOT_BTN_NORMAL;
        if (i == g_selected_tab) {
            state = BOT_BTN_ACTIVE;
        } else if (i == g_hovered_tab) {
            state = BOT_BTN_HOVER;
        }

        bot_draw_button(g_canvas, tx, ty, tw, th_btn, g_tab_labels[i], state);
    }

    /* Vertical Separator separating Sidebar and Main View */
    bot_canvas_draw_line(g_canvas, SIDEBAR_W, side_y, SIDEBAR_W, side_y + side_h, th->panel_border);

    /* Render Right Panel Content based on selected tab */
    int content_x = SIDEBAR_W + 15;
    int content_y = side_y + 20;

    switch (g_selected_tab) {
        case 0:
            render_overview(g_canvas, content_x, content_y, th);
            break;
        case 1:
            render_commands(g_canvas, content_x, content_y, th);
            break;
        case 2:
            render_rpg(g_canvas, content_x, content_y, th);
            break;
        case 3:
            render_gui_apps(g_canvas, content_x, content_y, th);
            break;
        case 4:
            render_system_sdk(g_canvas, content_x, content_y, th);
            break;
        default:
            break;
    }

    /* Bottom status bar */
    bot_draw_separator(g_canvas, 0, g_height - STATUS_HEIGHT - 1, g_width);
    
    char status[128];
    snprintf(status, sizeof(status), " Documentation Category: %s | Press ESC to Close", g_tab_labels[g_selected_tab]);
    bot_draw_status_bar(g_canvas, 0, g_height - STATUS_HEIGHT, g_width, STATUS_HEIGHT, status);

    /* Copy Canvas Pixels to Window Framebuffer and Flip */
    uint32_t *fb = bot_window_get_framebuffer(g_window);
    uint32_t *px = bot_canvas_get_pixels(g_canvas);
    if (fb && px) {
        memcpy(fb, px, (size_t)(g_width * g_height) * sizeof(uint32_t));
    }
    bot_window_flip(g_window);
    g_needs_redraw = 0;
}

static void on_mouse_down(const bot_event_t *ev, void *data)
{
    (void)data;
    if (ev->mouse.button == BOT_MOUSE_LEFT) {
        int mx = ev->mouse.x;
        int my = ev->mouse.y;
        int side_y = TITLE_H;

        /* Check tab button clicks */
        for (int i = 0; i < TAB_COUNT; i++) {
            int ty = side_y + 12 + i * 36;
            int tx = 10;
            int tw = SIDEBAR_W - 20;
            int th_btn = 28;

            if (bot_button_hit(tx, ty, tw, th_btn, mx, my)) {
                g_selected_tab = i;
                g_needs_redraw = 1;
                break;
            }
        }
    }
}

static void on_mouse_move(const bot_event_t *ev, void *data)
{
    (void)data;
    int mx = ev->mouse.x;
    int my = ev->mouse.y;
    int side_y = TITLE_H;
    int prev_hover = g_hovered_tab;

    g_hovered_tab = -1;
    for (int i = 0; i < TAB_COUNT; i++) {
        int ty = side_y + 12 + i * 36;
        int tx = 10;
        int tw = SIDEBAR_W - 20;
        int th_btn = 28;

        if (bot_button_hit(tx, ty, tw, th_btn, mx, my)) {
            g_hovered_tab = i;
            break;
        }
    }

    if (g_hovered_tab != prev_hover) {
        g_needs_redraw = 1;
    }
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
    /* Keep window.c's own framebuffer in sync with the new size —
     * otherwise the blit at the end of render() overflows it as soon
     * as the window manager resizes this window. */
    bot_window_resize(g_window, g_width, g_height);
    bot_canvas_destroy(g_canvas);
    g_canvas = bot_canvas_create(g_width, g_height);
    g_needs_redraw = 1;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    bot_log_init(NULL, BOT_LOG_INFO);

    if (bot_ui_init() != 0) {
        return 1;
    }

    g_window = bot_window_create("BotInfo", g_width, g_height);
    g_canvas = bot_canvas_create(g_width, g_height);
    if (!g_window || !g_canvas) {
        bot_ui_shutdown();
        return 1;
    }

    /* Load current theme configurations */
    bot_theme_load();

    bot_event_on(BOT_EVENT_MOUSE_DOWN, on_mouse_down, NULL);
    bot_event_on(BOT_EVENT_MOUSE_MOVE, on_mouse_move, NULL);
    bot_event_on(BOT_EVENT_KEY_DOWN, on_key_down, NULL);
    bot_event_on(BOT_EVENT_RESIZE, on_resize, NULL);

    bot_window_show(g_window);
    render_info();

    bot_event_t event;
    while (!bot_window_should_close(g_window)) {
        while (bot_event_poll(&event)) {
            if (event.type == BOT_EVENT_CLOSE) {
                goto cleanup;
            }
        }
        if (g_needs_redraw) {
            render_info();
        }
        bot_event_wait(&event);
        if (event.type == BOT_EVENT_CLOSE) {
            break;
        }
    }

cleanup:
    bot_canvas_destroy(g_canvas);
    bot_window_destroy(g_window);
    bot_ui_shutdown();
    bot_log_shutdown();
    return 0;
}
