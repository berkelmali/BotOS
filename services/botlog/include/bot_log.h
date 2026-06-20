/* ============================================================
 * BotOS Core — Centralized Logging API
 * ============================================================
 * File:    bot_log.h
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Async ring-buffer logger with severity levels, timestamps,
 * file rotation, and formatted output. Used system-wide by
 * all BotOS components for consistent debug/audit logging.
 *
 * Usage:
 *   #include "bot_log.h"
 *   bot_log_init("/var/log/botos", BOT_LOG_DEBUG);
 *   BOT_LOG_INFO("System started on port %d", 8080);
 *   bot_log_shutdown();
 * ============================================================ */

#ifndef BOTOS_LOG_H
#define BOTOS_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Log Levels ──────────────────────────────────────────── */

typedef enum {
    BOT_LOG_TRACE = 0,   /**< Extremely verbose tracing.   */
    BOT_LOG_DEBUG = 1,   /**< Debug-level messages.        */
    BOT_LOG_INFO  = 2,   /**< Informational messages.      */
    BOT_LOG_WARN  = 3,   /**< Warning conditions.          */
    BOT_LOG_ERROR = 4,   /**< Error conditions.            */
    BOT_LOG_FATAL = 5,   /**< Fatal / unrecoverable.       */
} bot_log_level_t;

/* ── Lifecycle ───────────────────────────────────────────── */

/**
 * Initialize the logging subsystem.
 *
 * @param log_dir    Directory for log files (NULL = stderr only).
 * @param min_level  Minimum level to output.
 * @return           0 on success, -1 on error.
 */
int bot_log_init(const char *log_dir, bot_log_level_t min_level);

/**
 * Shut down logging. Flushes remaining buffer and closes files.
 */
void bot_log_shutdown(void);

/* ── Logging Functions ───────────────────────────────────── */

/**
 * Log a message with the given level.
 *
 * @param level   Log severity.
 * @param file    Source file name (__FILE__).
 * @param line    Source line number (__LINE__).
 * @param fmt     Printf-format string.
 * @param ...     Format arguments.
 */
void bot_log_write(bot_log_level_t level, const char *file,
                   int line, const char *fmt, ...);

/**
 * Log with va_list (for wrapper functions).
 */
void bot_log_writev(bot_log_level_t level, const char *file,
                    int line, const char *fmt, va_list args);

/**
 * Set the minimum log level at runtime.
 */
void bot_log_set_level(bot_log_level_t level);

/**
 * Enable or disable colored output.
 */
void bot_log_set_color(int enabled);

/* ── Convenience Macros ──────────────────────────────────── */

#define BOT_LOG_TRACE(...) \
    bot_log_write(BOT_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)

#define BOT_LOG_DEBUG(...) \
    bot_log_write(BOT_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#define BOT_LOG_INFO(...) \
    bot_log_write(BOT_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)

#define BOT_LOG_WARN(...) \
    bot_log_write(BOT_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)

#define BOT_LOG_ERROR(...) \
    bot_log_write(BOT_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#define BOT_LOG_FATAL(...) \
    bot_log_write(BOT_LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_LOG_H */
