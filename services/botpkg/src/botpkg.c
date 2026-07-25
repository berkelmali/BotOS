/* ============================================================
 * BotOS Core — Package Manager CLI (Production)
 * ============================================================
 * File:    botpkg.c
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production package manager.
 *
 * Features:
 *   - Install with dependency resolution (topological order)
 *   - Download .botpkg archives via BotNet HTTP
 *   - Local package database in /var/lib/botpkg/<name>/
 *   - Remove with reverse-dependency safety check
 *   - Update via version comparison
 *   - List installed packages with version/description
 *   - Search remote repository (stub, ready for BotNet)
 *   - Colored CLI output with progress indicators
 * ============================================================ */

#include "botpkg.h"
#include "manifest.h"
#include "resolver.h"
#include "bot_log.h"
#include "bot_http.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

/* ── Constants ───────────────────────────────────────────── */

#define BOTPKG_VERSION     "0.3.0"
#define BOTPKG_REPO_URL    "http://repo.botos.dev/packages"

/* ── Color Codes ─────────────────────────────────────────── */

#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_CYAN    "\033[36m"
#define CLR_GREEN   "\033[32m"
#define CLR_RED     "\033[31m"
#define CLR_YELLOW  "\033[33m"
#define CLR_DIM     "\033[2m"

/* ── State ───────────────────────────────────────────────── */

static char g_db_dir[256]    = "/var/lib/botpkg";
static char g_cache_dir[256] = "/var/cache/botpkg";
static int  g_initialized    = 0;

/* ── Internal: Directory Helpers ─────────────────────────── */

/**
 * Create a directory (and parents) if it doesn't exist.
 * Simple single-level mkdir with mode 0755.
 */
static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;  /* Already exists */
    }
    return mkdir(path, 0755);
}

/* ── Internal: Package Database ──────────────────────────── */

/**
 * Register a package in the local database.
 * Creates <db_dir>/<name>/ and writes manifest.json.
 */
static int db_register_package(const bot_package_t *pkg)
{
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", g_db_dir, pkg->name);

    if (ensure_dir(pkg_dir) != 0 && errno != EEXIST) {
        BOT_LOG_ERROR("Failed to create package dir: %s", pkg_dir);
        return -1;
    }

    /* Write manifest */
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", pkg_dir);

    char *json = manifest_to_json(pkg);
    if (!json) return -1;

    FILE *fp = fopen(manifest_path, "w");
    if (!fp) {
        free(json);
        return -1;
    }

    fputs(json, fp);
    fclose(fp);
    free(json);

    return 0;
}

/**
 * Unregister a package from the local database.
 * Removes <db_dir>/<name>/manifest.json and the directory.
 */
static int db_unregister_package(const char *name)
{
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s/manifest.json", g_db_dir, name);

    unlink(manifest_path);

    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", g_db_dir, name);
    rmdir(pkg_dir);  /* Only succeeds if dir is empty */

    return 0;
}

/**
 * Check if any installed package depends on the given name.
 * Returns the name of the first dependent found, or NULL.
 */
static const char *db_find_reverse_dep(const char *name)
{
    static char dep_name[64];

    DIR *dir = opendir(g_db_dir);
    if (!dir) return NULL;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char manifest_path[1024];
        snprintf(manifest_path, sizeof(manifest_path),
                 "%s/%s/manifest.json", g_db_dir, entry->d_name);

        bot_package_t pkg;
        if (manifest_parse(manifest_path, &pkg) != 0) continue;

        for (int i = 0; i < pkg.dep_count; i++) {
            if (strcmp(pkg.deps[i], name) == 0) {
                strncpy(dep_name, entry->d_name, 63);
                dep_name[63] = '\0';
                free(pkg.deps);
                closedir(dir);
                return dep_name;
            }
        }

        free(pkg.deps);
    }

    closedir(dir);
    return NULL;
}

/* ── Lifecycle ───────────────────────────────────────────── */

int botpkg_init(const char *db_dir, const char *cache_dir)
{
    if (g_initialized) return 0;

    const char *env_db = getenv("BOTPKG_DB_DIR");
    const char *env_cache = getenv("BOTPKG_CACHE_DIR");

    if (db_dir) {
        strncpy(g_db_dir, db_dir, sizeof(g_db_dir) - 1);
    } else if (env_db) {
        strncpy(g_db_dir, env_db, sizeof(g_db_dir) - 1);
    }
    g_db_dir[sizeof(g_db_dir) - 1] = '\0';

    if (cache_dir) {
        strncpy(g_cache_dir, cache_dir, sizeof(g_cache_dir) - 1);
    } else if (env_cache) {
        strncpy(g_cache_dir, env_cache, sizeof(g_cache_dir) - 1);
    }
    g_cache_dir[sizeof(g_cache_dir) - 1] = '\0';

    /* Ensure directories exist */
    ensure_dir(g_db_dir);
    ensure_dir(g_cache_dir);

    BOT_LOG_INFO("BotPkg initialized (db=%s, cache=%s)", g_db_dir, g_cache_dir);
    g_initialized = 1;
    return 0;
}

void botpkg_shutdown(void)
{
    if (!g_initialized) return;
    g_initialized = 0;
}

/* ── Operations ──────────────────────────────────────────── */

int botpkg_install(const char *name)
{
    if (!name || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    printf("\n  %s%sInstalling:%s %s\n\n", CLR_BOLD, CLR_CYAN, CLR_RESET, name);

    /* Step 1: Resolve dependency tree */
    resolve_result_t deps;
    if (resolver_resolve(name, &deps) != 0) {
        printf("  %s[FAIL]%s Failed to resolve dependencies for '%s'\n\n",
               CLR_RED, CLR_RESET, name);
        return -1;
    }

    /* Step 2: Show resolution plan */
    int to_install = 0;
    for (int i = 0; i < deps.count; i++) {
        if (!resolver_is_installed(deps.names[i])) {
            to_install++;
        }
    }

    if (to_install == 0 && resolver_is_installed(name)) {
        printf("  %s[skip]%s %s is already installed\n\n",
               CLR_YELLOW, CLR_RESET, name);
        resolver_free_result(&deps);
        return 0;
    }

    if (deps.count > 1) {
        printf("  Dependency tree (%d packages):\n", deps.count);
        for (int i = 0; i < deps.count; i++) {
            const char *status = resolver_is_installed(deps.names[i])
                ? "installed" : "NEW";
            printf("    %s%-20s%s  %s%s%s\n",
                   CLR_BOLD, deps.names[i], CLR_RESET,
                   CLR_DIM, status, CLR_RESET);
        }
        printf("\n");
    }

    /* Step 3: Install each dependency in topological order */
    int installed = 0;
    for (int i = 0; i < deps.count; i++) {
        if (resolver_is_installed(deps.names[i])) {
            printf("  %s[skip]%s  %-20s  (already installed)\n",
                   CLR_DIM, CLR_RESET, deps.names[i]);
            continue;
        }

        printf("  %s[%d/%d]%s  Installing %s ...",
               CLR_CYAN, installed + 1, to_install, CLR_RESET,
               deps.names[i]);
        fflush(stdout);

        /* Create a minimal package entry in the database */
        bot_package_t pkg;
        memset(&pkg, 0, sizeof(pkg));
        strncpy(pkg.name, deps.names[i], sizeof(pkg.name) - 1);
        strncpy(pkg.version, "0.0.0", sizeof(pkg.version));

        const char *repo_url = getenv("BOTPKG_REPO_URL");
        if (!repo_url) {
            repo_url = "http://repo.botos.dev/packages";
        }

        char cache_pkg_dir[512];
        snprintf(cache_pkg_dir, sizeof(cache_pkg_dir), "%s/%s", g_cache_dir, deps.names[i]);
        ensure_dir(cache_pkg_dir);

        char cache_manifest[512];
        snprintf(cache_manifest, sizeof(cache_manifest),
                 "%s/%s/manifest.json", g_cache_dir, deps.names[i]);

        /* Fetch the manifest for THIS install — previously this only
         * ever read from the local cache, which nothing in this
         * function populated, so pkg stayed at the placeholder
         * name/version="0.0.0" defaults above and pkg.checksum was
         * always empty (silently skipping verification) on any fresh
         * install that hadn't separately run `botpkg update` first. */
        char manifest_url[1024];
        snprintf(manifest_url, sizeof(manifest_url), "%s/%s/manifest.json",
                 repo_url, deps.names[i]);
        if (bot_http_download(manifest_url, cache_manifest) != 0) {
            BOT_LOG_WARN("Could not fetch manifest for '%s' — "
                         "installing with placeholder metadata, no checksum verification",
                         deps.names[i]);
        }
        manifest_parse(cache_manifest, &pkg);
        /* manifest_parse() may have overwritten name/version with
         * parsed-but-empty fields on a partial/malformed manifest —
         * make sure the identity fields this loop depends on can
         * never end up blank. */
        if (pkg.name[0] == '\0') strncpy(pkg.name, deps.names[i], sizeof(pkg.name) - 1);
        if (pkg.version[0] == '\0') strncpy(pkg.version, "0.0.0", sizeof(pkg.version));

        /* Download the package archive from remote repository */
        char archive_url[1024];
        snprintf(archive_url, sizeof(archive_url), "%s/%s/%s.botpkg",
                 repo_url, deps.names[i], deps.names[i]);

        char cache_archive_path[512];
        snprintf(cache_archive_path, sizeof(cache_archive_path), "%s/%s/%s.botpkg",
                 g_cache_dir, deps.names[i], deps.names[i]);

        /* Download archive */
        int dl_ok = (bot_http_download(archive_url, cache_archive_path) == 0);
        if (!dl_ok) {
            printf(" %s[FAIL]%s (download failed: %s)\n", CLR_RED, CLR_RESET, archive_url);
            free(pkg.deps);
            continue; /* previously fell through to db_register_package() regardless */
        }

        /* Verify integrity if the manifest declared a checksum. Repos
         * that don't publish one yet (pkg.checksum[0] == '\0') get the
         * same behavior as before this existed — this is additive,
         * not a new requirement every repo must meet immediately. */
        if (pkg.checksum[0] != '\0') {
            char actual_hex[SHA256_HEX_SIZE];
            if (sha256_file_hex(cache_archive_path, actual_hex) != 0) {
                printf(" %s[FAIL]%s (could not hash downloaded archive)\n", CLR_RED, CLR_RESET);
                remove(cache_archive_path);
                free(pkg.deps);
                continue;
            }
            if (strcmp(actual_hex, pkg.checksum) != 0) {
                printf(" %s[FAIL]%s (checksum mismatch — expected %.8s..., got %.8s...; archive discarded)\n",
                       CLR_RED, CLR_RESET, pkg.checksum, actual_hex);
                remove(cache_archive_path); /* don't leave a corrupt/tampered archive in the cache */
                free(pkg.deps);
                continue;
            }
        }

        /* Register in database */
        if (db_register_package(&pkg) == 0) {
            printf(" %s[OK]%s\n", CLR_GREEN, CLR_RESET);
            installed++;
        } else {
            printf(" %s[FAIL]%s\n", CLR_RED, CLR_RESET);
        }

        free(pkg.deps);
    }

    resolver_free_result(&deps);

    printf("\n  %s%s+%s %d package(s) installed successfully.\n\n",
           CLR_BOLD, CLR_GREEN, CLR_RESET, installed);
    return 0;
}

int botpkg_remove(const char *name)
{
    if (!name || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    printf("\n  %s%sRemoving:%s %s\n\n", CLR_BOLD, CLR_CYAN, CLR_RESET, name);

    if (!resolver_is_installed(name)) {
        printf("  %s[FAIL]%s '%s' is not installed\n\n",
               CLR_RED, CLR_RESET, name);
        return -1;
    }

    /* Check reverse dependencies */
    const char *rdep = db_find_reverse_dep(name);
    if (rdep) {
        printf("  %s[FAIL]%s Cannot remove '%s': required by '%s'\n",
               CLR_RED, CLR_RESET, name, rdep);
        printf("  %sRemove '%s' first, or use --force%s\n\n",
               CLR_DIM, rdep, CLR_RESET);
        return -1;
    }

    /* Remove from database */
    db_unregister_package(name);

    printf("  %s[OK]%s   Removed %s\n\n", CLR_GREEN, CLR_RESET, name);
    return 0;
}

int botpkg_update(const char *name)
{
    if (!name || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    printf("\n  %s%sUpdating:%s %s\n\n", CLR_BOLD, CLR_CYAN, CLR_RESET, name);

    if (!resolver_is_installed(name)) {
        printf("  %s[FAIL]%s '%s' is not installed\n\n",
               CLR_RED, CLR_RESET, name);
        return -1;
    }

    /* Load current manifest */
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s/manifest.json", g_db_dir, name);

    bot_package_t current;
    if (manifest_parse(manifest_path, &current) == 0) {
        printf("  Current version: %s%s%s\n", CLR_BOLD, current.version, CLR_RESET);
        free(current.deps);
    }

    /* Fetch latest manifest from BOTPKG_REPO_URL */
    const char *repo_url = getenv("BOTPKG_REPO_URL");
    if (!repo_url) {
        repo_url = "http://repo.botos.dev/packages";
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/%s/manifest.json", repo_url, name);

    char cache_manifest_path[512];
    snprintf(cache_manifest_path, sizeof(cache_manifest_path), "%s/%s/manifest.json.new",
             g_cache_dir, name);

    /* Ensure package cache directory exists */
    char cache_pkg_dir[512];
    snprintf(cache_pkg_dir, sizeof(cache_pkg_dir), "%s/%s", g_cache_dir, name);
    ensure_dir(cache_pkg_dir);

    int dl_ok = (bot_http_download(url, cache_manifest_path) == 0);
    if (dl_ok) {
        bot_package_t latest;
        memset(&latest, 0, sizeof(latest));
        if (manifest_parse(cache_manifest_path, &latest) == 0) {
            printf("  Latest version:  %s%s%s\n", CLR_BOLD, latest.version, CLR_RESET);
            
            char db_manifest_path[512];
            snprintf(db_manifest_path, sizeof(db_manifest_path), "%s/%s/manifest.json", g_db_dir, name);
            
            /* Overwrite with latest manifest */
            if (rename(cache_manifest_path, db_manifest_path) == 0) {
                printf("  %s[OK]%s   Updated %s to version %s\n\n", CLR_GREEN, CLR_RESET, name, latest.version);
            } else {
                printf("  %s[FAIL]%s Failed to install updated manifest\n\n", CLR_RED, CLR_RESET);
            }
            free(latest.deps);
            return 0;
        }
        printf("  %s[FAIL]%s Downloaded manifest could not be parsed\n\n", CLR_RED, CLR_RESET);
        remove(cache_manifest_path);
        return -1;
    }
    /* Previously this fell through to the same "is up to date" message
     * printed below regardless of whether the download actually
     * succeeded — a network failure or repo outage was reported to the
     * user identically to "nothing to do", masking real problems. */
    printf("  %s[FAIL]%s Could not reach repository to check for updates: %s\n\n",
           CLR_RED, CLR_RESET, url);
    return -1;
}

int botpkg_list_installed(void)
{
    printf("\n  %s%sBotPkg — Installed Packages%s\n", CLR_BOLD, CLR_CYAN, CLR_RESET);
    printf("  %s-------------------------------------------%s\n", CLR_DIM, CLR_RESET);

    DIR *dir = opendir(g_db_dir);
    if (!dir) {
        printf("  (package database not found)\n\n");
        return 0;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        /* Check if it's a directory with a manifest */
        char manifest_path[1024];
        snprintf(manifest_path, sizeof(manifest_path),
                 "%s/%s/manifest.json", g_db_dir, entry->d_name);

        bot_package_t pkg;
        if (manifest_parse(manifest_path, &pkg) == 0) {
            printf("  %s%-20s%s  %sv%s%s",
                   CLR_BOLD, pkg.name, CLR_RESET,
                   CLR_GREEN, pkg.version, CLR_RESET);

            if (pkg.description[0] != '\0') {
                printf("  %s%s%s", CLR_DIM, pkg.description, CLR_RESET);
            }
            printf("\n");

            free(pkg.deps);
            count++;
        }
    }

    closedir(dir);

    if (count == 0) {
        printf("  (no packages installed)\n");
    } else {
        printf("  %s-------------------------------------------%s\n", CLR_DIM, CLR_RESET);
        printf("  %d package(s) installed\n", count);
    }
    printf("\n");

    return 0;
}

int botpkg_search(const char *query)
{
    if (!query) return -1;

    printf("\n  %s%sSearch results for '%s'%s\n",
           CLR_BOLD, CLR_CYAN, query, CLR_RESET);
    printf("  %s-------------------------------------------%s\n", CLR_DIM, CLR_RESET);

    const char *repo_url = getenv("BOTPKG_REPO_URL");
    if (!repo_url) {
        repo_url = "http://repo.botos.dev/packages";
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/search?q=%s", repo_url, query);

    bot_http_response_t resp;
    if (bot_http_get(url, &resp) == 0) {
        if (resp.status_code == 200 && resp.body) {
            printf("%s\n", resp.body);
        } else {
            printf("  No packages found (HTTP %d)\n\n", resp.status_code);
        }
        bot_http_response_free(&resp);
    } else {
        printf("  Failed to query remote repository.\n\n");
    }
    return 0;
}

int botpkg_info(const char *name, bot_package_t *out)
{
    if (!name) return -1;

    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s/manifest.json", g_db_dir, name);

    bot_package_t pkg;
    if (manifest_parse(manifest_path, &pkg) != 0) {
        printf("  Package '%s' not found in local database.\n", name);
        return -1;
    }

    printf("\n  %s%sPackage: %s%s\n", CLR_BOLD, CLR_CYAN, pkg.name, CLR_RESET);
    printf("  %s-------------------------------------------%s\n", CLR_DIM, CLR_RESET);
    printf("  %-15s %s\n", "Version:", pkg.version);
    printf("  %-15s %s\n", "Author:", pkg.author[0] ? pkg.author : "(unknown)");
    printf("  %-15s %s\n", "Description:", pkg.description[0] ? pkg.description : "(none)");
    printf("  %-15s %d\n", "Dependencies:", pkg.dep_count);

    for (int i = 0; i < pkg.dep_count; i++) {
        printf("    %s- %s%s\n", CLR_DIM, pkg.deps[i], CLR_RESET);
    }
    printf("\n");

    if (out) {
        memcpy(out, &pkg, sizeof(bot_package_t));
        /* Note: caller inherits ownership of pkg.deps */
    } else {
        free(pkg.deps);
    }

    return 0;
}

/* ── CLI Entry Point ─────────────────────────────────────── */

static void print_usage(void)
{
    printf("\n");
    printf("  %s%sBotPkg%s — BotOS Package Manager v%s\n",
           CLR_BOLD, CLR_CYAN, CLR_RESET, BOTPKG_VERSION);
    printf("  %s============================================%s\n\n", CLR_DIM, CLR_RESET);
    printf("  Usage: botpkg <command> [arguments]\n\n");
    printf("  %sCommands:%s\n", CLR_BOLD, CLR_RESET);
    printf("    %sinstall%s <name>    Install a package (with dependencies)\n",
           CLR_GREEN, CLR_RESET);
    printf("    %sremove%s  <name>    Remove a package (checks reverse deps)\n",
           CLR_RED, CLR_RESET);
    printf("    %supdate%s  <name>    Update a package to latest version\n",
           CLR_YELLOW, CLR_RESET);
    printf("    %slist%s              List installed packages\n",
           CLR_CYAN, CLR_RESET);
    printf("    %ssearch%s  <query>   Search the repository\n",
           CLR_CYAN, CLR_RESET);
    printf("    %sinfo%s    <name>    Show package details\n",
           CLR_CYAN, CLR_RESET);
    printf("    %shelp%s              Show this help\n",
           CLR_DIM, CLR_RESET);
    printf("\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage();
        return 0;
    }

    bot_log_init(NULL, BOT_LOG_INFO);
    botpkg_init(NULL, NULL);

    int ret = 0;
    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "  botpkg: missing package name\n");
            ret = 1;
        } else {
            /* Support installing multiple packages */
            for (int i = 2; i < argc; i++) {
                if (botpkg_install(argv[i]) != 0) ret = 1;
            }
        }
    } else if (strcmp(cmd, "remove") == 0) {
        if (argc < 3) {
            fprintf(stderr, "  botpkg: missing package name\n");
            ret = 1;
        } else {
            for (int i = 2; i < argc; i++) {
                if (botpkg_remove(argv[i]) != 0) ret = 1;
            }
        }
    } else if (strcmp(cmd, "update") == 0) {
        if (argc < 3) {
            fprintf(stderr, "  botpkg: missing package name\n");
            ret = 1;
        } else {
            ret = botpkg_update(argv[2]);
        }
    } else if (strcmp(cmd, "list") == 0) {
        ret = botpkg_list_installed();
    } else if (strcmp(cmd, "search") == 0 && argc >= 3) {
        ret = botpkg_search(argv[2]);
    } else if (strcmp(cmd, "info") == 0 && argc >= 3) {
        ret = botpkg_info(argv[2], NULL);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
    } else if (strcmp(cmd, "--version") == 0) {
        printf("BotPkg v%s\n", BOTPKG_VERSION);
    } else {
        fprintf(stderr, "  botpkg: unknown command '%s'\n", cmd);
        print_usage();
        ret = 1;
    }

    botpkg_shutdown();
    bot_log_shutdown();
    return ret;
}
