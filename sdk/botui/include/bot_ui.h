/* ============================================================
 * BotOS Core — BotUI Umbrella Header
 * ============================================================
 * File:    bot_ui.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Single-include for the BotUI GUI abstraction library.
 * ============================================================ */

#ifndef BOTOS_UI_H
#define BOTOS_UI_H

#include "bot_window.h"
#include "bot_canvas.h"
#include "bot_event.h"
#include "bot_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the BotUI subsystem.
 * Sets up the display backend (X11, SDL2, or framebuffer).
 *
 * @return  0 on success, -1 on error.
 */
int bot_ui_init(void);

/**
 * Shut down the BotUI subsystem.
 */
void bot_ui_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_UI_H */
