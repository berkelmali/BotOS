/* ============================================================
 * BotOS Core — Manifest Parser (Production)
 * ============================================================
 * File:    manifest.c
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production JSON manifest parser.
 *
 * Parses manifest.json files for .botpkg packages without
 * any external JSON library. Hand-written tokenizer that
 * handles:
 *   - String values with escape sequences
 *   - String arrays (for dependencies)
 *   - Nested whitespace tolerance
 *   - Validation of required fields
 *
 * Example manifest.json:
 *   {
 *     "name": "botos-utils",
 *     "version": "1.2.0",
 *     "description": "Core utilities for BotOS",
 *     "author": "Berk Elmalı",
 *     "dependencies": ["libc-bot", "botnet-core"]
 *   }
 * ============================================================ */

#include "manifest.h"
#include "bot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ── Constants ───────────────────────────────────────────── */

#define MANIFEST_MAX_FILE_SIZE  (64 * 1024)  /* 64 KB max */
#define MANIFEST_MAX_DEPS       32

/* ── Internal: JSON Mini-Parser ──────────────────────────── */

/**
 * Skip whitespace in JSON.
 */
static const char *json_skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/**
 * Extract a quoted string value starting at p (pointing to opening ").
 * Writes the unescaped value into out and returns pointer past closing ".
 */
static const char *json_read_string(const char *p, char *out, size_t out_size)
{
    if (*p != '"') return NULL;
    p++;  /* Skip opening quote */

    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;  /* Skip backslash */
            switch (*p) {
                case '"':  out[i++] = '"';  break;
                case '\\': out[i++] = '\\'; break;
                case '/':  out[i++] = '/';  break;
                case 'n':  out[i++] = '\n'; break;
                case 't':  out[i++] = '\t'; break;
                case 'r':  out[i++] = '\r'; break;
                default:   out[i++] = *p;   break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }

    out[i] = '\0';

    if (*p == '"') p++;  /* Skip closing quote */
    return p;
}

/**
 * Find a key in JSON and position past the colon.
 * Returns pointer to the value portion, or NULL if not found.
 */
static const char *json_find_key(const char *json, const char *key)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *pos = json;

    while ((pos = strstr(pos, pattern)) != NULL) {
        /* Verify this is a key, not a value (preceded by { or , after ws) */
        pos += strlen(pattern);

        /* Skip whitespace after key */
        pos = json_skip_ws(pos);

        /* Expect colon */
        if (*pos == ':') {
            pos++;
            pos = json_skip_ws(pos);
            return pos;
        }
    }

    return NULL;
}

/**
 * Extract a string value for a given key.
 */
static int json_get_string(const char *json, const char *key,
                           char *out, size_t out_size)
{
    const char *val = json_find_key(json, key);
    if (!val || *val != '"') return -1;

    json_read_string(val, out, out_size);
    return 0;
}

/**
 * Parse a JSON string array: ["item1", "item2", ...]
 * Returns the number of items parsed.
 */
static int json_get_string_array(const char *json, const char *key,
                                 char (*out)[64], int max_items)
{
    const char *val = json_find_key(json, key);
    if (!val || *val != '[') return 0;

    val++;  /* Skip [ */
    int count = 0;

    while (*val && *val != ']' && count < max_items) {
        val = json_skip_ws(val);

        if (*val == '"') {
            val = json_read_string(val, out[count], 64);
            if (!val) break;
            count++;
        }

        val = json_skip_ws(val);
        if (*val == ',') val++;  /* Skip comma */
    }

    return count;
}

/* ── Public API ──────────────────────────────────────────── */

int manifest_parse(const char *path, bot_package_t *out)
{
    if (!path || !out) {
        errno = EINVAL;
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        BOT_LOG_ERROR("Cannot open manifest: %s: %s", path, strerror(errno));
        return -1;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || (size_t)fsize > MANIFEST_MAX_FILE_SIZE) {
        BOT_LOG_ERROR("Manifest too large or empty: %s (%ld bytes)", path, fsize);
        fclose(fp);
        return -1;
    }

    /* Read entire file */
    char *json = (char *)malloc((size_t)fsize + 1);
    if (!json) {
        fclose(fp);
        return -1;
    }

    size_t read_size = fread(json, 1, (size_t)fsize, fp);
    json[read_size] = '\0';
    fclose(fp);

    int ret = manifest_parse_string(json, out);
    free(json);
    return ret;
}

int manifest_parse_string(const char *json, bot_package_t *out)
{
    if (!json || !out) {
        errno = EINVAL;
        return -1;
    }

    memset(out, 0, sizeof(bot_package_t));

    /* Extract required fields */
    json_get_string(json, "name",        out->name,        sizeof(out->name));
    json_get_string(json, "version",     out->version,     sizeof(out->version));
    json_get_string(json, "description", out->description, sizeof(out->description));
    json_get_string(json, "author",      out->author,      sizeof(out->author));

    /* Validate required field */
    if (out->name[0] == '\0') {
        BOT_LOG_ERROR("Manifest missing required field: 'name'");
        return -1;
    }

    if (out->version[0] == '\0') {
        /* Default version if not specified */
        strncpy(out->version, "0.0.0", sizeof(out->version));
    }

    /* Parse dependencies array */
    char dep_buf[MANIFEST_MAX_DEPS][64];
    int dep_count = json_get_string_array(json, "dependencies",
                                          dep_buf, MANIFEST_MAX_DEPS);

    if (dep_count > 0) {
        out->deps = (char (*)[64])malloc((size_t)dep_count * sizeof(char[64]));
        if (!out->deps) {
            out->dep_count = 0;
            return -1;
        }

        for (int i = 0; i < dep_count; i++) {
            strncpy(out->deps[i], dep_buf[i], 63);
            out->deps[i][63] = '\0';
        }
        out->dep_count = dep_count;
    }

    BOT_LOG_DEBUG("Parsed manifest: %s v%s (%d deps)",
                  out->name, out->version, out->dep_count);
    return 0;
}

char *manifest_to_json(const bot_package_t *pkg)
{
    if (!pkg) return NULL;

    /* Calculate needed size */
    size_t needed = 512;  /* Base structure */
    if (pkg->dep_count > 0) {
        needed += (size_t)pkg->dep_count * 80;
    }

    char *json = (char *)malloc(needed);
    if (!json) return NULL;

    int pos = 0;

    pos += snprintf(json + pos, needed - (size_t)pos,
        "{\n"
        "  \"name\": \"%s\",\n"
        "  \"version\": \"%s\",\n"
        "  \"description\": \"%s\",\n"
        "  \"author\": \"%s\",\n"
        "  \"dependencies\": [",
        pkg->name, pkg->version, pkg->description, pkg->author);

    /* Write dependencies array */
    for (int i = 0; i < pkg->dep_count; i++) {
        if (i > 0) {
            pos += snprintf(json + pos, needed - (size_t)pos, ",");
        }
        pos += snprintf(json + pos, needed - (size_t)pos,
                        "\n    \"%s\"", pkg->deps[i]);
    }

    if (pkg->dep_count > 0) {
        pos += snprintf(json + pos, needed - (size_t)pos, "\n  ");
    }

    snprintf(json + pos, needed - (size_t)pos, "]\n}\n");

    return json;
}
