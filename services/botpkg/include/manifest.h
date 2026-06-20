/* ============================================================
 * BotOS Core — Manifest Parser API
 * ============================================================
 * File:    manifest.h
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_MANIFEST_H
#define BOTOS_MANIFEST_H

#include "botpkg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a manifest.json file into a package structure.
 * @param path     Path to manifest.json.
 * @param out      Output package info.
 * @return         0 on success, -1 on error.
 */
int manifest_parse(const char *path, bot_package_t *out);

/**
 * Parse manifest from a JSON string.
 * @param json     JSON string.
 * @param out      Output package info.
 * @return         0 on success, -1 on error.
 */
int manifest_parse_string(const char *json, bot_package_t *out);

/**
 * Serialize a package to JSON string.
 * Caller must free the returned string.
 * @param pkg      Package to serialize.
 * @return         Heap-allocated JSON string, or NULL.
 */
char *manifest_to_json(const bot_package_t *pkg);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_MANIFEST_H */
