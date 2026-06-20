/* ============================================================
 * BotOS Core — Package Manager API
 * ============================================================
 * File:    botpkg.h
 * Layer:   L5 — Platform Services
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#ifndef BOTOS_PKG_H
#define BOTOS_PKG_H

#ifdef __cplusplus
extern "C" {
#endif

/** Package info structure. */
typedef struct bot_package {
    char    name[64];
    char    version[32];
    char    description[256];
    char    author[64];
    int     dep_count;
    char  (*deps)[64];   /**< Array of dependency names. */
} bot_package_t;

/** Initialize the package manager. */
int botpkg_init(const char *db_dir, const char *cache_dir);

/** Shut down the package manager. */
void botpkg_shutdown(void);

/** Install a package by name. */
int botpkg_install(const char *name);

/** Remove an installed package. */
int botpkg_remove(const char *name);

/** Update a package to latest version. */
int botpkg_update(const char *name);

/** List all installed packages. */
int botpkg_list_installed(void);

/** Search for packages in the repository. */
int botpkg_search(const char *query);

/** Get info about an installed package. */
int botpkg_info(const char *name, bot_package_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_PKG_H */
