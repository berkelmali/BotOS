/* ============================================================
 * BotOS Core — Memory Utilities
 * ============================================================
 * File:    bot_mem.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Memory pool allocator and tracking helpers. Provides
 * arena-style allocation for fast bulk alloc/free patterns.
 * ============================================================ */

#ifndef BOTOS_MEM_H
#define BOTOS_MEM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Memory Pool ─────────────────────────────────────────── */

/** Opaque memory pool handle. */
typedef struct bot_pool bot_pool_t;

/**
 * Create a new memory pool.
 *
 * @param block_size  Size of each block in the pool.
 * @param max_blocks  Maximum number of blocks.
 * @return            Pool handle, or NULL on error.
 */
bot_pool_t *bot_pool_create(size_t block_size, size_t max_blocks);

/**
 * Allocate a block from the pool.
 *
 * @param pool  Pool handle.
 * @return      Pointer to allocated block, or NULL if full.
 */
void *bot_pool_alloc(bot_pool_t *pool);

/**
 * Return a block to the pool.
 *
 * @param pool  Pool handle.
 * @param ptr   Pointer previously returned by bot_pool_alloc.
 */
void bot_pool_free(bot_pool_t *pool, void *ptr);

/**
 * Destroy a pool and free all memory.
 *
 * @param pool  Pool handle.
 */
void bot_pool_destroy(bot_pool_t *pool);

/**
 * Get number of allocated blocks in a pool.
 *
 * @param pool  Pool handle.
 * @return      Number of currently allocated blocks.
 */
size_t bot_pool_used(const bot_pool_t *pool);

/* ── Tracked Allocation ──────────────────────────────────── */

/**
 * Allocate memory with tracking (reports leaks on shutdown).
 *
 * @param size   Number of bytes.
 * @param file   Source file name (__FILE__).
 * @param line   Source line number (__LINE__).
 * @return       Pointer to allocated memory.
 */
void *bot_malloc_tracked(size_t size, const char *file, int line);

/**
 * Free tracked memory.
 *
 * @param ptr  Pointer from bot_malloc_tracked.
 */
void bot_free_tracked(void *ptr);

/**
 * Report memory leak summary to stderr.
 */
void bot_mem_report(void);

/* ── Convenience Macros ──────────────────────────────────── */

#ifdef BOTOS_MEM_TRACKING
    #define bot_malloc(size)  bot_malloc_tracked((size), __FILE__, __LINE__)
    #define bot_free(ptr)     bot_free_tracked(ptr)
#else
    #include <stdlib.h>
    #define bot_malloc(size)  malloc(size)
    #define bot_free(ptr)     free(ptr)
#endif

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_MEM_H */
