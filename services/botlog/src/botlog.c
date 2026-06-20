/* ============================================================
 * BotOS Core — BotLog Implementation
 * ============================================================
 * File:    botlog.c
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "bot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* ── State ───────────────────────────────────────────────── */

static bot_log_level_t g_min_level   = BOT_LOG_INFO;
static FILE           *g_log_file    = NULL;
static int             g_color       = 1;
static int             g_initialized = 0;
static char            g_log_path[512];

/* ── Level Names & Colors ────────────────────────────────── */

static const char *level_names[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
    "\033[37m",    /* TRACE: white/dim  */
    "\033[36m",    /* DEBUG: cyan       */
    "\033[32m",    /* INFO:  green      */
    "\033[33m",    /* WARN:  yellow     */
    "\033[31m",    /* ERROR: red        */
    "\033[35;1m",  /* FATAL: bold magenta */
};

/* ── Internal Helpers ────────────────────────────────────── */

static void get_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

/* ── Public API ──────────────────────────────────────────── */

int bot_log_init(const char *log_dir, bot_log_level_t min_level)
{
    if (g_initialized) return 0;

    g_min_level = min_level;

    if (log_dir) {
        snprintf(g_log_path, sizeof(g_log_path), "%s/botos.log", log_dir);
        g_log_file = fopen(g_log_path, "a");
        if (!g_log_file) {
            fprintf(stderr, "[BotLog] Warning: Cannot open %s, using stderr\n",
                    g_log_path);
        }
    }

    g_initialized = 1;
    return 0;
}

void bot_log_shutdown(void)
{
    if (!g_initialized) return;

    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }

    g_initialized = 0;
}

void bot_log_set_level(bot_log_level_t level)
{
    g_min_level = level;
}

void bot_log_set_color(int enabled)
{
    g_color = enabled;
}

void bot_log_write(bot_log_level_t level, const char *file,
                   int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bot_log_writev(level, file, line, fmt, args);
    va_end(args);
}

void bot_log_writev(bot_log_level_t level, const char *file,
                    int line, const char *fmt, va_list args)
{
    if (level < g_min_level) return;

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    const char *fname = basename_of(file);
    const char *lname = level_names[level];

    /* Format the user message */
    char message[2048];
    vsnprintf(message, sizeof(message), fmt, args);

    /* Write to stderr with optional color */
    if (g_color) {
        fprintf(stderr, "%s%s %-5s%s %s:%d — %s\n",
                level_colors[level], timestamp, lname,
                "\033[0m", fname, line, message);
    } else {
        fprintf(stderr, "%s %-5s %s:%d — %s\n",
                timestamp, lname, fname, line, message);
    }

    /* Write to log file (no colors) */
    if (g_log_file) {
        fprintf(g_log_file, "%s %-5s %s:%d — %s\n",
                timestamp, lname, fname, line, message);
        fflush(g_log_file);
    }
}
