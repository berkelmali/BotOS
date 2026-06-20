/* ============================================================
 * BotOS Core — Dependency Resolver API
 * ============================================================
 * File:    resolver.h
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_RESOLVER_H
#define BOTOS_RESOLVER_H

#include "botpkg.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Resolved dependency list (topologically sorted). */
typedef struct resolve_result {
    int    count;
    char (*names)[64];   /**< Package names in install order. */
} resolve_result_t;

/**
 * Resolve the full dependency tree for a package.
 * Returns a topologically sorted list of all packages
 * that need to be installed (including transitive deps).
 *
 * @param name    Root package name.
 * @param result  Output: resolved dependency list.
 * @return        0 on success, -1 on error (e.g., cycle).
 */
int resolver_resolve(const char *name, resolve_result_t *result);

/**
 * Check if a package is already installed.
 * @param name  Package name.
 * @return      1 if installed, 0 if not.
 */
int resolver_is_installed(const char *name);

/** Free a resolve_result_t. */
void resolver_free_result(resolve_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_RESOLVER_H */
