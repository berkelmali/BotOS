/* ============================================================
 * BotOS Core — libbot Umbrella Header
 * ============================================================
 * File:    bot.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Single-include header for the BotOS SDK core library.
 * Includes all libbot sub-modules: I/O, strings, memory.
 *
 * Usage:
 *   #include "bot.h"
 * ============================================================ */

#ifndef BOTOS_BOT_H
#define BOTOS_BOT_H

#include "bot_io.h"
#include "bot_string.h"
#include "bot_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── SDK Version ─────────────────────────────────────────── */

#define BOTOS_SDK_VERSION_MAJOR  0
#define BOTOS_SDK_VERSION_MINOR  3
#define BOTOS_SDK_VERSION_PATCH  0
#define BOTOS_SDK_VERSION_STRING "0.3.0"

/**
 * Get the SDK version string at runtime.
 * @return  Version string (e.g., "0.3.0").
 */
const char *bot_sdk_version(void);

/**
 * Initialize all SDK subsystems.
 * Calls bot_io_init(), bot_mem_init(), etc.
 *
 * @return  0 on success, -1 on error.
 */
int bot_sdk_init(void);

/**
 * Shut down all SDK subsystems.
 */
void bot_sdk_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_BOT_H */
