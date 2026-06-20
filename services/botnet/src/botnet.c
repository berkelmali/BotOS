/* ============================================================
 * BotOS Core — BotNet Initialization (Production)
 * ============================================================
 * File:    botnet.c
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Network subsystem lifecycle.
 *
 * Features:
 *   - Network interface detection via getifaddrs()
 *   - DNS resolver availability check
 *   - Subsystem state tracking
 *   - Graceful shutdown with resource cleanup
 * ============================================================ */

#include "botnet.h"
#include "bot_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

/* ── State ───────────────────────────────────────────────── */

static int g_net_initialized  = 0;
static int g_has_interfaces   = 0;
static int g_has_dns          = 0;

/* ── Internal: Network Interface Detection ───────────────── */

/**
 * Detect active non-loopback network interfaces.
 * Uses getifaddrs() to enumerate and log interfaces.
 */
static int net_detect_interfaces(void)
{
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        BOT_LOG_ERROR("getifaddrs failed: %s", strerror(errno));
        return 0;
    }

    int count = 0;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;

        /* Only count IPv4 and IPv6 */
        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;

        char addr_buf[64] = {0};

        if (family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, addr_buf, sizeof(addr_buf));
        } else {
            struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &sa6->sin6_addr, addr_buf, sizeof(addr_buf));
        }

        /* Skip loopback */
        if (strcmp(addr_buf, "127.0.0.1") == 0 || strcmp(addr_buf, "::1") == 0) {
            continue;
        }

        BOT_LOG_DEBUG("  Interface: %s  addr=%s  family=%s",
                      ifa->ifa_name, addr_buf,
                      family == AF_INET ? "IPv4" : "IPv6");
        count++;
    }

    freeifaddrs(ifaddr);
    return count;
}

/**
 * Check if DNS resolution is available by resolving localhost.
 */
static int net_check_dns(void)
{
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo("localhost", "80", &hints, &result);
    if (ret == 0) {
        freeaddrinfo(result);
        return 1;
    }

    return 0;
}

/* ── Public API ──────────────────────────────────────────── */

int bot_net_init(void)
{
    if (g_net_initialized) return 0;

    BOT_LOG_INFO("BotNet subsystem initializing...");

    /* Detect network interfaces */
    int iface_count = net_detect_interfaces();
    g_has_interfaces = (iface_count > 0);

    if (g_has_interfaces) {
        BOT_LOG_INFO("  Found %d active network interface(s)", iface_count);
    } else {
        BOT_LOG_WARN("  No active network interfaces detected");
    }

    /* Check DNS resolver */
    g_has_dns = net_check_dns();
    if (g_has_dns) {
        BOT_LOG_INFO("  DNS resolver: available");
    } else {
        BOT_LOG_WARN("  DNS resolver: unavailable");
    }

    g_net_initialized = 1;
    BOT_LOG_INFO("BotNet subsystem initialized");
    return 0;
}

void bot_net_shutdown(void)
{
    if (!g_net_initialized) return;

    BOT_LOG_INFO("BotNet subsystem shutting down");
    g_net_initialized = 0;
    g_has_interfaces  = 0;
    g_has_dns         = 0;
}

int bot_net_is_available(void)
{
    return g_net_initialized && g_has_interfaces;
}
