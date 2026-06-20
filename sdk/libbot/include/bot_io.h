/* ============================================================
 * BotOS Core — I/O Utilities
 * ============================================================
 * File:    bot_io.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * High-level file I/O wrappers built on top of the VFS.
 * Provides convenient read-entire-file, write-string, and
 * file-existence checking without manual fd management.
 * ============================================================ */

#ifndef BOTOS_IO_H
#define BOTOS_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read entire file contents into a heap-allocated buffer.
 * Caller must free() the returned buffer.
 *
 * @param path      File path (routed through VFS).
 * @param out_size  Output: number of bytes read (may be NULL).
 * @return          Heap buffer with file contents, or NULL on error.
 */
char *bot_io_read_file(const char *path, size_t *out_size);

/**
 * Write a string to a file (creates or truncates).
 *
 * @param path  File path.
 * @param data  Null-terminated string to write.
 * @return      Number of bytes written, -1 on error.
 */
int bot_io_write_file(const char *path, const char *data);

/**
 * Append a string to a file.
 *
 * @param path  File path.
 * @param data  Null-terminated string to append.
 * @return      Number of bytes written, -1 on error.
 */
int bot_io_append_file(const char *path, const char *data);

/**
 * Check if a file exists.
 *
 * @param path  File path.
 * @return      1 if exists, 0 if not.
 */
int bot_io_exists(const char *path);

/**
 * Get file size in bytes.
 *
 * @param path  File path.
 * @return      Size in bytes, -1 on error.
 */
int64_t bot_io_file_size(const char *path);

/**
 * Copy a file from src to dst.
 *
 * @param src  Source path.
 * @param dst  Destination path.
 * @return     0 on success, -1 on error.
 */
int bot_io_copy(const char *src, const char *dst);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_IO_H */
