/* ============================================================
 * BotOS Core — SHA-256 implementation (FIPS 180-4)
 * ============================================================
 * File:    sha256.c
 * License: MIT
 * ============================================================ */

#include "sha256.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Round constants: first 32 bits of the fractional parts of the
 * cube roots of the first 64 primes. */
static const unsigned int K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static unsigned int rotr(unsigned int x, int n)
{
    return (x >> n) | (x << (32 - n));
}

/* Compresses exactly one 64-byte (512-bit) block into ctx->state. */
static void sha256_process_block(sha256_ctx_t *ctx, const unsigned char block[64])
{
    unsigned int w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((unsigned int)block[i*4]     << 24) |
               ((unsigned int)block[i*4 + 1] << 16) |
               ((unsigned int)block[i*4 + 2] << 8)  |
               ((unsigned int)block[i*4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        unsigned int s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        unsigned int s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19)  ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    unsigned int a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    unsigned int e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        unsigned int S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        unsigned int ch  = (e & f) ^ (~e & g);
        unsigned int temp1 = h + S1 + ch + K[i] + w[i];
        unsigned int S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned int temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx)
{
    /* First 32 bits of the fractional parts of the square roots of
     * the first 8 primes. */
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->bitlen = 0;
    ctx->buffer_len = 0;
}

void sha256_update(sha256_ctx_t *ctx, const unsigned char *data, size_t len)
{
    size_t i = 0;
    while (i < len) {
        size_t take = 64 - ctx->buffer_len;
        if (take > len - i) take = len - i;
        memcpy(ctx->buffer + ctx->buffer_len, data + i, take);
        ctx->buffer_len += (unsigned int)take;
        i += take;

        if (ctx->buffer_len == 64) {
            sha256_process_block(ctx, ctx->buffer);
            ctx->bitlen += 512;
            ctx->buffer_len = 0;
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, unsigned char digest[SHA256_DIGEST_SIZE])
{
    /* Capture the true message length (in bits) before any padding
     * touches ctx->bitlen/buffer_len via sha256_update()'s normal
     * bookkeeping. */
    unsigned long long total_bits = ctx->bitlen + (unsigned long long)ctx->buffer_len * 8;

    /* Mandatory 0x80 padding byte, then zero bytes until the buffer
     * holds exactly 56 bytes — leaving exactly 8 bytes of room in the
     * final block for the length field below. */
    unsigned char pad = 0x80;
    sha256_update(ctx, &pad, 1);

    unsigned char zero = 0x00;
    while (ctx->buffer_len != 56) {
        sha256_update(ctx, &zero, 1);
    }

    unsigned char len_bytes[8];
    for (int i = 0; i < 8; i++) {
        len_bytes[i] = (unsigned char)(total_bits >> (56 - i * 8));
    }
    sha256_update(ctx, len_bytes, 8); /* buffer_len hits 64 -> final block processed */

    for (int i = 0; i < 8; i++) {
        digest[i*4]     = (unsigned char)(ctx->state[i] >> 24);
        digest[i*4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        digest[i*4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        digest[i*4 + 3] = (unsigned char)(ctx->state[i]);
    }
}

void sha256_to_hex(const unsigned char digest[SHA256_DIGEST_SIZE], char out_hex[SHA256_HEX_SIZE])
{
    static const char hexchars[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        out_hex[i*2]     = hexchars[(digest[i] >> 4) & 0xF];
        out_hex[i*2 + 1] = hexchars[digest[i] & 0xF];
    }
    out_hex[SHA256_HEX_SIZE - 1] = '\0';
}

int sha256_file_hex(const char *path, char out_hex[SHA256_HEX_SIZE])
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    sha256_ctx_t ctx;
    sha256_init(&ctx);

    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sha256_update(&ctx, buf, n);
    }
    if (ferror(f)) {
        fclose(f);
        errno = EIO;
        return -1;
    }
    fclose(f);

    unsigned char digest[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, digest);
    sha256_to_hex(digest, out_hex);
    return 0;
}
