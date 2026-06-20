/* ============================================================
 * BotOS Core — String Utilities Implementation
 * ============================================================
 * File:    bot_string.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "bot_string.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

size_t bot_strlcpy(char *dst, const char *src, size_t size)
{
    size_t src_len = strlen(src);
    if (size > 0) {
        size_t copy_len = (src_len < size - 1) ? src_len : size - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

size_t bot_strlcat(char *dst, const char *src, size_t size)
{
    size_t dst_len = strlen(dst);
    size_t src_len = strlen(src);

    if (dst_len >= size) return size + src_len;

    size_t remaining = size - dst_len - 1;
    size_t copy_len = (src_len < remaining) ? src_len : remaining;
    memcpy(dst + dst_len, src, copy_len);
    dst[dst_len + copy_len] = '\0';

    return dst_len + src_len;
}

char *bot_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = (char *)malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

char *bot_strndup(const char *s, size_t n)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len > n) len = n;
    char *dup = (char *)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}

char *bot_strtrim(char *s)
{
    if (!s) return NULL;

    /* Trim leading whitespace */
    while (*s && isspace((unsigned char)*s)) s++;

    /* Trim trailing whitespace */
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return s;
}

int bot_str_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    size_t plen = strlen(prefix);
    return (strncmp(s, prefix, plen) == 0) ? 1 : 0;
}

int bot_str_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return 0;
    size_t slen = strlen(s);
    size_t xlen = strlen(suffix);
    if (xlen > slen) return 0;
    return (strcmp(s + slen - xlen, suffix) == 0) ? 1 : 0;
}

char **bot_strsplit(const char *s, char delim, int *count)
{
    if (!s || !count) return NULL;

    /* Count delimiters */
    int n = 1;
    for (const char *p = s; *p; p++) {
        if (*p == delim) n++;
    }

    char **tokens = (char **)malloc(sizeof(char *) * (size_t)(n + 1));
    if (!tokens) return NULL;

    const char *start = s;
    int idx = 0;

    for (const char *p = s; ; p++) {
        if (*p == delim || *p == '\0') {
            size_t len = (size_t)(p - start);
            tokens[idx] = (char *)malloc(len + 1);
            if (tokens[idx]) {
                memcpy(tokens[idx], start, len);
                tokens[idx][len] = '\0';
            }
            idx++;
            if (*p == '\0') break;
            start = p + 1;
        }
    }

    tokens[idx] = NULL;
    *count = idx;
    return tokens;
}

void bot_strsplit_free(char **tokens, int count)
{
    if (!tokens) return;
    for (int i = 0; i < count; i++) {
        free(tokens[i]);
    }
    free(tokens);
}
