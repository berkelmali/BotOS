/* ============================================================
 * BotOS Core — BotPkg SHA-256 Unit Tests
 * ============================================================
 * File:    test_botpkg.c
 * License: MIT
 *
 * Covers sha256.c, added so downloaded .botpkg archives can be
 * checked against the checksum declared in their manifest. Verified
 * against the standard published FIPS 180-4 / NIST test vectors, plus
 * a cross-check against Python's hashlib during development (see
 * durum2.md for that transcript and for the separate botpkg_install()
 * integration test — a checksum-mismatch/failed-download rejection
 * test — that isn't part of this suite because exercising it needs a
 * stand-in for bot_net's HTTP layer, which itself needs OpenSSL
 * headers this test suite doesn't otherwise require).
 * ============================================================ */

#include "sha256.h"
#include <stdio.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  [TEST] %-40s ", #name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("\033[32mPASS\033[0m\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("\033[31mFAIL\033[0m — %s\n", msg); \
    } while(0)

static int hash_string_matches(const char *input, const char *expected_hex)
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char *)input, strlen(input));
    unsigned char digest[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, digest);
    char hex[SHA256_HEX_SIZE];
    sha256_to_hex(digest, hex);
    return strcmp(hex, expected_hex) == 0;
}

/* FIPS 180-4 / NIST published test vector. */
static void test_sha256_empty_string(void)
{
    TEST(sha256_empty_string);
    if (hash_string_matches("",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
        PASS();
    } else {
        FAIL("empty-string digest did not match the published test vector");
    }
}

/* FIPS 180-4 / NIST published test vector. */
static void test_sha256_abc(void)
{
    TEST(sha256_abc);
    if (hash_string_matches("abc",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        PASS();
    } else {
        FAIL("\"abc\" digest did not match the published test vector");
    }
}

/* FIPS 180-4 / NIST published test vector — 448 bits, i.e. exactly the
 * single-block/padding boundary case. */
static void test_sha256_448bit_boundary(void)
{
    TEST(sha256_448bit_boundary);
    if (hash_string_matches("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")) {
        PASS();
    } else {
        FAIL("448-bit boundary digest did not match the published test vector");
    }
}

/* Exercises the multi-block chaining path (input > 64 bytes), which
 * the vectors above don't reach on their own. Cross-checked against
 * Python's hashlib.sha256(b'a'*1000) during development. */
static void test_sha256_multiblock(void)
{
    TEST(sha256_multiblock);
    char input[1000];
    memset(input, 'a', sizeof(input));

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char *)input, sizeof(input));
    unsigned char digest[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, digest);
    char hex[SHA256_HEX_SIZE];
    sha256_to_hex(digest, hex);

    const char *expected = "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3";
    if (strcmp(hex, expected) == 0) {
        PASS();
    } else {
        FAIL("1000-byte multi-block digest did not match the cross-checked value");
    }
}

/* Feeding the same bytes to sha256_update() in several small calls
 * must give the same digest as one large call — this is what
 * sha256_file_hex() relies on internally, chunking through a file in
 * a fixed-size read buffer rather than loading it all at once. */
static void test_sha256_incremental_matches_single_call(void)
{
    TEST(sha256_incremental_matches_single_call);
    const char *msg = "The quick brown fox jumps over the lazy dog";

    sha256_ctx_t ctx_whole;
    sha256_init(&ctx_whole);
    sha256_update(&ctx_whole, (const unsigned char *)msg, strlen(msg));
    unsigned char digest_whole[SHA256_DIGEST_SIZE];
    sha256_final(&ctx_whole, digest_whole);
    char hex_whole[SHA256_HEX_SIZE];
    sha256_to_hex(digest_whole, hex_whole);

    sha256_ctx_t ctx_chunked;
    sha256_init(&ctx_chunked);
    for (size_t i = 0; i < strlen(msg); i++) {
        sha256_update(&ctx_chunked, (const unsigned char *)&msg[i], 1);
    }
    unsigned char digest_chunked[SHA256_DIGEST_SIZE];
    sha256_final(&ctx_chunked, digest_chunked);
    char hex_chunked[SHA256_HEX_SIZE];
    sha256_to_hex(digest_chunked, hex_chunked);

    if (strcmp(hex_whole, hex_chunked) == 0) {
        PASS();
    } else {
        FAIL("one-shot and byte-at-a-time hashing of the same message disagreed");
    }
}

static void test_sha256_file_hex(void)
{
    TEST(sha256_file_hex);
    const char *path = "/tmp/botos_test_sha256_file.txt";
    FILE *f = fopen(path, "wb");
    if (!f) {
        FAIL("could not create temp file for the test");
        return;
    }
    const char *content = "BotOS test file for checksum verification";
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    char hex[SHA256_HEX_SIZE];
    int rc = sha256_file_hex(path, hex);
    remove(path);

    if (rc != 0) {
        FAIL("sha256_file_hex() returned an error on a file that exists");
        return;
    }
    if (hash_string_matches(content, hex)) {
        PASS();
    } else {
        FAIL("sha256_file_hex() result did not match hashing the same bytes in memory");
    }
}

static void test_sha256_file_hex_missing_file(void)
{
    TEST(sha256_file_hex_missing_file);
    char hex[SHA256_HEX_SIZE];
    int rc = sha256_file_hex("/tmp/botos_test_sha256_this_should_not_exist.txt", hex);
    if (rc != 0) {
        PASS();
    } else {
        FAIL("sha256_file_hex() should report an error for a nonexistent file");
    }
}

int main(void)
{
    printf("\n=== BotPkg SHA-256 Tests ===\n\n");

    test_sha256_empty_string();
    test_sha256_abc();
    test_sha256_448bit_boundary();
    test_sha256_multiblock();
    test_sha256_incremental_matches_single_call();
    test_sha256_file_hex();
    test_sha256_file_hex_missing_file();

    printf("\n  Results: %d/%d passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
