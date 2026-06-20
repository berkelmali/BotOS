/* ============================================================
 * BotOS Core — Memory Utilities Implementation
 * ============================================================
 * File:    bot_mem.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "bot_mem.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>    /* uint8_t  */
#include <stddef.h>    /* ptrdiff_t */

/* ── Memory Pool Implementation ──────────────────────────── */

struct bot_pool {
    size_t   block_size;
    size_t   max_blocks;
    size_t   used;
    uint8_t *memory;       /* Contiguous backing memory      */
    int     *free_map;     /* 0 = free, 1 = allocated        */
};

bot_pool_t *bot_pool_create(size_t block_size, size_t max_blocks)
{
    if (block_size == 0 || max_blocks == 0) return NULL;

    bot_pool_t *pool = (bot_pool_t *)calloc(1, sizeof(bot_pool_t));
    if (!pool) return NULL;

    pool->block_size = block_size;
    pool->max_blocks = max_blocks;
    pool->used       = 0;

    pool->memory = (uint8_t *)calloc(max_blocks, block_size);
    if (!pool->memory) {
        free(pool);
        return NULL;
    }

    pool->free_map = (int *)calloc(max_blocks, sizeof(int));
    if (!pool->free_map) {
        free(pool->memory);
        free(pool);
        return NULL;
    }

    return pool;
}

void *bot_pool_alloc(bot_pool_t *pool)
{
    if (!pool || pool->used >= pool->max_blocks) return NULL;

    for (size_t i = 0; i < pool->max_blocks; i++) {
        if (!pool->free_map[i]) {
            pool->free_map[i] = 1;
            pool->used++;
            return pool->memory + (i * pool->block_size);
        }
    }

    return NULL;
}

void bot_pool_free(bot_pool_t *pool, void *ptr)
{
    if (!pool || !ptr) return;

    /* Validate ptr is within our pool */
    ptrdiff_t offset = (uint8_t *)ptr - pool->memory;
    if (offset < 0 || (size_t)offset >= pool->max_blocks * pool->block_size) {
        return;  /* Not our pointer */
    }

    size_t idx = (size_t)offset / pool->block_size;
    if (pool->free_map[idx]) {
        pool->free_map[idx] = 0;
        pool->used--;
        memset(ptr, 0, pool->block_size);  /* Zero out for safety */
    }
}

void bot_pool_destroy(bot_pool_t *pool)
{
    if (!pool) return;
    free(pool->free_map);
    free(pool->memory);
    free(pool);
}

size_t bot_pool_used(const bot_pool_t *pool)
{
    return pool ? pool->used : 0;
}

/* ── Tracked Allocation ──────────────────────────────────── */

#define BOT_MEM_TRACK_MAX  4096

typedef struct {
    void       *ptr;
    size_t      size;
    const char *file;
    int         line;
} mem_track_entry_t;

static mem_track_entry_t g_mem_track[BOT_MEM_TRACK_MAX];
static int               g_mem_track_count = 0;
static size_t            g_mem_total_alloc  = 0;
static size_t            g_mem_total_freed  = 0;

void *bot_malloc_tracked(size_t size, const char *file, int line)
{
    void *ptr = malloc(size);
    if (!ptr) return NULL;

    if (g_mem_track_count < BOT_MEM_TRACK_MAX) {
        g_mem_track[g_mem_track_count].ptr  = ptr;
        g_mem_track[g_mem_track_count].size = size;
        g_mem_track[g_mem_track_count].file = file;
        g_mem_track[g_mem_track_count].line = line;
        g_mem_track_count++;
    }

    g_mem_total_alloc += size;
    return ptr;
}

void bot_free_tracked(void *ptr)
{
    if (!ptr) return;

    for (int i = 0; i < g_mem_track_count; i++) {
        if (g_mem_track[i].ptr == ptr) {
            g_mem_total_freed += g_mem_track[i].size;
            /* Swap with last entry */
            g_mem_track[i] = g_mem_track[g_mem_track_count - 1];
            g_mem_track_count--;
            break;
        }
    }

    free(ptr);
}

void bot_mem_report(void)
{
    fprintf(stderr, "\n  ── BotOS Memory Report ──\n");
    fprintf(stderr, "  Total allocated : %zu bytes\n", g_mem_total_alloc);
    fprintf(stderr, "  Total freed     : %zu bytes\n", g_mem_total_freed);
    fprintf(stderr, "  Outstanding     : %d allocation(s)\n", g_mem_track_count);

    if (g_mem_track_count > 0) {
        fprintf(stderr, "\n  ⚠  Potential Leaks:\n");
        for (int i = 0; i < g_mem_track_count && i < 20; i++) {
            fprintf(stderr, "    %p  %6zu bytes  at %s:%d\n",
                    g_mem_track[i].ptr,
                    g_mem_track[i].size,
                    g_mem_track[i].file,
                    g_mem_track[i].line);
        }
        if (g_mem_track_count > 20) {
            fprintf(stderr, "    ... and %d more\n", g_mem_track_count - 20);
        }
    }

    fprintf(stderr, "\n");
}
