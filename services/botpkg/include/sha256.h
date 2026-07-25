/* ============================================================
 * BotOS Core — SHA-256
 * ============================================================
 * File:    sha256.h
 * Layer:   L5 — Platform Services
 * License: MIT
 *
 * A small, self-contained SHA-256 implementation (FIPS 180-4),
 * used only to verify downloaded .botpkg archives against the
 * checksum declared in their manifest.
 *
 * This is deliberately NOT built on OpenSSL: bot_net/http.c already
 * needs OpenSSL for TLS, but that dev toolchain isn't always present
 * everywhere BotPkg needs to build (this is true right now of the
 * sandbox this file was written and tested in), and checksum
 * verification is simple enough not to need a general-purpose crypto
 * library. This only implements what BotPkg needs — a one-shot
 * digest of a byte buffer or file, printed as lowercase hex — not a
 * general hashing API.
 * ============================================================ */

#ifndef BOTOS_SHA256_H
#define BOTOS_SHA256_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE    (SHA256_DIGEST_SIZE * 2 + 1) /* +1 for '\0' */

typedef struct {
    unsigned int  state[8];
    unsigned long long bitlen;
    unsigned char buffer[64];
    unsigned int  buffer_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const unsigned char *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, unsigned char digest[SHA256_DIGEST_SIZE]);

/** Convenience: hex-encode a digest (lowercase, NUL-terminated). */
void sha256_to_hex(const unsigned char digest[SHA256_DIGEST_SIZE],
                    char out_hex[SHA256_HEX_SIZE]);

/**
 * Hash an entire file's contents.
 * Returns 0 and fills out_hex on success, -1 on I/O error (errno set).
 */
int sha256_file_hex(const char *path, char out_hex[SHA256_HEX_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_SHA256_H */
