/* ============================================================
 * BotOS Core — Dependency Resolver (Production)
 * ============================================================
 * File:    resolver.c
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production dependency resolver.
 *
 * Algorithm: Depth-first topological sort with 3-color marking.
 *
 *   WHITE (0) = unvisited
 *   GRAY  (1) = visiting (on the DFS stack → cycle if revisited)
 *   BLACK (2) = fully resolved
 *
 * The resolver reads manifests from the local database to
 * discover transitive dependencies, then emits them in
 * install order (post-order = dependencies before dependents).
 *
 * Features:
 *   - Cycle detection with error reporting
 *   - Installed package registry with persistence
 *   - Transitive dependency resolution
 *   - Duplicate suppression in output
 * ============================================================ */

#include "resolver.h"
#include "manifest.h"
#include "bot_log.h"
#include "bot_http.h"
#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ── Constants ───────────────────────────────────────────── */

#define MAX_INSTALLED      256
#define MAX_RESOLVE_DEPTH  64
#define PKG_NAME_MAX       64

/* ── Node Colors for DFS ─────────────────────────────────── */

#define COLOR_WHITE  0  /* Unvisited  */
#define COLOR_GRAY   1  /* Visiting   */
#define COLOR_BLACK  2  /* Resolved   */

/* ── Installed Package Registry ──────────────────────────── */

static char g_installed[MAX_INSTALLED][PKG_NAME_MAX];
static int  g_installed_count = 0;

static const char *get_db_dir(void)
{
    const char *db_dir = getenv("BOTPKG_DB_DIR");
    if (!db_dir) db_dir = "/var/lib/botpkg";
    return db_dir;
}

static const char *get_cache_dir(void)
{
    const char *cache_dir = getenv("BOTPKG_CACHE_DIR");
    if (!cache_dir) cache_dir = "/var/cache/botpkg";
    return cache_dir;
}

/* ── Internal: DFS State ─────────────────────────────────── */

typedef struct {
    char    names[MAX_RESOLVE_DEPTH][PKG_NAME_MAX];
    int     colors[MAX_RESOLVE_DEPTH];
    int     node_count;

    /* Output: topologically sorted result (post-order) */
    char    output[MAX_RESOLVE_DEPTH][PKG_NAME_MAX];
    int     output_count;

    /* Error state */
    int     has_cycle;
    char    cycle_from[PKG_NAME_MAX];
    char    cycle_to[PKG_NAME_MAX];
} dfs_state_t;

/* ── Internal: Node Management ───────────────────────────── */

/**
 * Find or create a node in the DFS state by name.
 * @return  Node index, or -1 if table full.
 */
static int dfs_get_node(dfs_state_t *state, const char *name)
{
    /* Search existing */
    for (int i = 0; i < state->node_count; i++) {
        if (strcmp(state->names[i], name) == 0) {
            return i;
        }
    }

    /* Create new */
    if (state->node_count >= MAX_RESOLVE_DEPTH) {
        BOT_LOG_ERROR("Dependency tree too deep (max %d packages)", MAX_RESOLVE_DEPTH);
        return -1;
    }

    int idx = state->node_count;
    strncpy(state->names[idx], name, PKG_NAME_MAX - 1);
    state->names[idx][PKG_NAME_MAX - 1] = '\0';
    state->colors[idx] = COLOR_WHITE;
    state->node_count++;

    return idx;
}

/**
 * Add a package to the output list (if not already present).
 */
static void dfs_emit(dfs_state_t *state, const char *name)
{
    /* Deduplicate */
    for (int i = 0; i < state->output_count; i++) {
        if (strcmp(state->output[i], name) == 0) return;
    }

    if (state->output_count < MAX_RESOLVE_DEPTH) {
        strncpy(state->output[state->output_count], name, PKG_NAME_MAX - 1);
        state->output[state->output_count][PKG_NAME_MAX - 1] = '\0';
        state->output_count++;
    }
}

/* ── Internal: Load Package Dependencies ─────────────────── */

/**
 * Try to load a package's manifest from the local database.
 * Looks for: <db_path>/<name>/manifest.json
 *
 * @return  0 on success, -1 if manifest not found (leaf package).
 */
static int load_package_deps(const char *name, bot_package_t *pkg)
{
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s/manifest.json", get_db_dir(), name);

    if (manifest_parse(manifest_path, pkg) == 0) {
        return 0;
    }

    /* Try cache path */
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path),
             "%s/%s/manifest.json", get_cache_dir(), name);

    if (manifest_parse(cache_path, pkg) == 0) {
        return 0;
    }

    /* Try downloading from remote repo */
    const char *repo_url = getenv("BOTPKG_REPO_URL");
    if (!repo_url) {
        repo_url = "http://repo.botos.dev/packages";
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/%s/manifest.json", repo_url, name);

    /* Ensure package cache dir exists */
    char cache_pkg_dir[512];
    snprintf(cache_pkg_dir, sizeof(cache_pkg_dir), "%s/%s", get_cache_dir(), name);
    
    struct stat st;
    if (stat(cache_pkg_dir, &st) != 0) {
        mkdir(cache_pkg_dir, 0755);
    }

    if (bot_http_download(url, cache_path) == 0) {
        return manifest_parse(cache_path, pkg);
    }

    return -1;
}

/* ── Internal: DFS Visit ─────────────────────────────────── */

/**
 * Recursive DFS visit for topological sort.
 *
 * @return  0 on success, -1 on cycle detection.
 */
static int dfs_visit(dfs_state_t *state, const char *name)
{
    if (state->has_cycle) return -1;

    int idx = dfs_get_node(state, name);
    if (idx < 0) return -1;

    if (state->colors[idx] == COLOR_BLACK) {
        /* Already fully resolved — skip */
        return 0;
    }

    if (state->colors[idx] == COLOR_GRAY) {
        /* Cycle detected! This node is on the current DFS path */
        state->has_cycle = 1;
        strncpy(state->cycle_to, name, PKG_NAME_MAX - 1);
        BOT_LOG_ERROR("Dependency cycle detected: %s -> %s",
                      state->cycle_from, name);
        return -1;
    }

    /* Mark as visiting (GRAY) */
    state->colors[idx] = COLOR_GRAY;
    strncpy(state->cycle_from, name, PKG_NAME_MAX - 1);

    /* Load this package's dependencies */
    bot_package_t pkg;
    if (load_package_deps(name, &pkg) == 0) {
        /* Recurse into each dependency */
        for (int i = 0; i < pkg.dep_count; i++) {
            if (dfs_visit(state, pkg.deps[i]) != 0) {
                /* Free deps before returning on error */
                free(pkg.deps);
                return -1;
            }
        }
        free(pkg.deps);
    }
    /* If manifest not found, treat as a leaf (no dependencies) */

    /* Post-order: emit after all dependencies are resolved */
    dfs_emit(state, name);

    /* Mark as fully resolved (BLACK) */
    state->colors[idx] = COLOR_BLACK;

    return 0;
}

/* ── Public API ──────────────────────────────────────────── */

int resolver_is_installed(const char *name)
{
    if (!name) return 0;

    for (int i = 0; i < g_installed_count; i++) {
        if (strcmp(g_installed[i], name) == 0) return 1;
    }

    /* Also check for manifest on disk */
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s/manifest.json", get_db_dir(), name);

    FILE *fp = fopen(manifest_path, "r");
    if (fp) {
        fclose(fp);

        /* Cache in memory */
        if (g_installed_count < MAX_INSTALLED) {
            strncpy(g_installed[g_installed_count], name, PKG_NAME_MAX - 1);
            g_installed[g_installed_count][PKG_NAME_MAX - 1] = '\0';
            g_installed_count++;
        }

        return 1;
    }

    return 0;
}

int resolver_resolve(const char *name, resolve_result_t *result)
{
    if (!name || !result) {
        errno = EINVAL;
        return -1;
    }

    memset(result, 0, sizeof(*result));

    /* Initialize DFS state */
    dfs_state_t *state = (dfs_state_t *)calloc(1, sizeof(dfs_state_t));
    if (!state) {
        errno = ENOMEM;
        return -1;
    }

    /* Run DFS from the root package */
    int ret = dfs_visit(state, name);

    if (ret != 0 || state->has_cycle) {
        if (state->has_cycle) {
            BOT_LOG_ERROR("Cannot resolve: circular dependency involving '%s'",
                          state->cycle_to);
        }
        free(state);
        errno = ELOOP;
        return -1;
    }

    /* Build result from DFS output */
    if (state->output_count > 0) {
        result->names = (char (*)[64])malloc(
            (size_t)state->output_count * sizeof(char[64]));

        if (!result->names) {
            free(state);
            errno = ENOMEM;
            return -1;
        }

        for (int i = 0; i < state->output_count; i++) {
            strncpy(result->names[i], state->output[i], 63);
            result->names[i][63] = '\0';
        }
        result->count = state->output_count;
    } else {
        /* At minimum, include the requested package itself */
        result->names = (char (*)[64])malloc(sizeof(char[64]));
        if (!result->names) {
            free(state);
            errno = ENOMEM;
            return -1;
        }
        strncpy(result->names[0], name, 63);
        result->names[0][63] = '\0';
        result->count = 1;
    }

    BOT_LOG_DEBUG("Resolved %s: %d package(s) in install order", name, result->count);

    for (int i = 0; i < result->count; i++) {
        BOT_LOG_DEBUG("  [%d] %s%s", i + 1, result->names[i],
                      resolver_is_installed(result->names[i]) ? " (installed)" : "");
    }

    free(state);
    return 0;
}

void resolver_free_result(resolve_result_t *result)
{
    if (!result) return;
    free(result->names);
    result->names = NULL;
    result->count = 0;
}
