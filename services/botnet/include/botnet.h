/* ============================================================
 * BotOS Core — Network API
 * ============================================================
 * File:    botnet.h
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_NET_H
#define BOTOS_NET_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the BotNet subsystem.
 * @return 0 on success, -1 on error.
 */
int bot_net_init(void);

/** Shut down the BotNet subsystem. */
void bot_net_shutdown(void);

/** Check if network is available. */
int bot_net_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_NET_H */
