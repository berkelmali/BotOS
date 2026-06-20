/* ============================================================
 * BotOS Core — String Utilities
 * ============================================================
 * File:    bot_string.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Safe string operations: bounded copy/concat, trimming,
 * splitting, and format helpers.
 * ============================================================ */

#ifndef BOTOS_STRING_H
#define BOTOS_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Safe bounded string copy (always null-terminates).
 *
 * @param dst   Destination buffer.
 * @param src   Source string.
 * @param size  Size of destination buffer.
 * @return      Length of src (for truncation detection).
 */
size_t bot_strlcpy(char *dst, const char *src, size_t size);

/**
 * Safe bounded string concatenation.
 *
 * @param dst   Destination buffer (must contain a string).
 * @param src   String to append.
 * @param size  Total size of destination buffer.
 * @return      Total length that would have been created.
 */
size_t bot_strlcat(char *dst, const char *src, size_t size);

/**
 * Duplicate a string (heap-allocated).
 *
 * @param s  String to duplicate.
 * @return   New heap string, or NULL on failure.
 */
char *bot_strdup(const char *s);

/**
 * Duplicate at most n characters of a string.
 *
 * @param s  String to duplicate.
 * @param n  Maximum characters to copy.
 * @return   New heap string, or NULL on failure.
 */
char *bot_strndup(const char *s, size_t n);

/**
 * Trim leading and trailing whitespace in-place.
 *
 * @param s  String to trim (modified in place).
 * @return   Pointer to the trimmed start (within s).
 */
char *bot_strtrim(char *s);

/**
 * Check if a string starts with a prefix.
 *
 * @param s       String to check.
 * @param prefix  Prefix to match.
 * @return        1 if starts with prefix, 0 otherwise.
 */
int bot_str_starts_with(const char *s, const char *prefix);

/**
 * Check if a string ends with a suffix.
 *
 * @param s       String to check.
 * @param suffix  Suffix to match.
 * @return        1 if ends with suffix, 0 otherwise.
 */
int bot_str_ends_with(const char *s, const char *suffix);

/**
 * Split a string by delimiter into an array of tokens.
 * Caller must free the returned array and each token.
 *
 * @param s      String to split.
 * @param delim  Delimiter character.
 * @param count  Output: number of tokens.
 * @return       Array of heap-allocated token strings.
 */
char **bot_strsplit(const char *s, char delim, int *count);

/**
 * Free a token array created by bot_strsplit().
 *
 * @param tokens  Array to free.
 * @param count   Number of tokens.
 */
void bot_strsplit_free(char **tokens, int count);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_STRING_H */
