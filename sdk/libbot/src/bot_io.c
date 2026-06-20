/* ============================================================
 * BotOS Core — I/O Utilities Implementation
 * ============================================================
 * File:    bot_io.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "bot_io.h"
#include "bot_vfs.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ── SDK Version (from bot.h) ────────────────────────────── */

const char *bot_sdk_version(void)
{
    return "0.3.0";
}

int bot_sdk_init(void)
{
    // TODO: Initialize all SDK subsystems
    return bot_vfs_init();
}

void bot_sdk_shutdown(void)
{
    bot_vfs_shutdown();
}

/* ── I/O Operations ──────────────────────────────────────── */

char *bot_io_read_file(const char *path, size_t *out_size)
{
    if (!path) return NULL;

    int fd = bot_open(path, BOT_O_RDONLY);
    if (fd < 0) return NULL;

    /* Get file size via stat */
    bot_stat_t st;
    if (bot_stat(path, &st) != 0) {
        bot_close(fd);
        return NULL;
    }

    size_t file_size = (size_t)st.size;
    char *buf = (char *)malloc(file_size + 1);
    if (!buf) {
        bot_close(fd);
        return NULL;
    }

    ssize_t total = 0;
    while ((size_t)total < file_size) {
        ssize_t n = bot_read(fd, buf + total, file_size - (size_t)total);
        if (n <= 0) break;
        total += n;
    }

    buf[total] = '\0';
    bot_close(fd);

    if (out_size) *out_size = (size_t)total;
    return buf;
}

int bot_io_write_file(const char *path, const char *data)
{
    if (!path || !data) return -1;

    int fd = bot_open(path, BOT_O_WRONLY | BOT_O_CREAT | BOT_O_TRUNC);
    if (fd < 0) return -1;

    size_t len = strlen(data);
    ssize_t written = bot_write(fd, data, len);
    bot_close(fd);

    return (int)written;
}

int bot_io_append_file(const char *path, const char *data)
{
    if (!path || !data) return -1;

    int fd = bot_open(path, BOT_O_WRONLY | BOT_O_CREAT | BOT_O_APPEND);
    if (fd < 0) return -1;

    size_t len = strlen(data);
    ssize_t written = bot_write(fd, data, len);
    bot_close(fd);

    return (int)written;
}

int bot_io_exists(const char *path)
{
    if (!path) return 0;
    bot_stat_t st;
    return (bot_stat(path, &st) == 0) ? 1 : 0;
}

int64_t bot_io_file_size(const char *path)
{
    if (!path) return -1;
    bot_stat_t st;
    if (bot_stat(path, &st) != 0) return -1;
    return (int64_t)st.size;
}

int bot_io_copy(const char *src, const char *dst)
{
    if (!src || !dst) return -1;

    size_t size = 0;
    char *data = bot_io_read_file(src, &size);
    if (!data) return -1;

    int fd = bot_open(dst, BOT_O_WRONLY | BOT_O_CREAT | BOT_O_TRUNC);
    if (fd < 0) {
        free(data);
        return -1;
    }

    ssize_t written = bot_write(fd, data, size);
    bot_close(fd);
    free(data);

    return (written == (ssize_t)size) ? 0 : -1;
}
