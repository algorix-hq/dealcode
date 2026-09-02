/*
 * Test suite for the dealcode C library.
 *
 * Covers:
 *  - the 9 official NIST FF1 sample vectors (via the private FF1 seam)
 *  - every config/vector/invalid-code/normalize case in testvectors/v1.json
 *  - buffer, range, and config error behaviour
 *  - large roundtrip loops across stage boundaries, including configs where
 *    radix^max_length is exactly 2^128
 *  - fixed-length cycling mode (SPEC section 11): every case in
 *    testvectors/v1c.json plus permutation/boundary/namespace behaviour
 *  - integer range mode (SPEC section 12): every case in
 *    testvectors/v1r.json plus bijection/dead-zone/binding behaviour
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dealcode.h"
#include "ff1.h" /* private test seam */
#include "vectors.inc"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                      \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
        }                                                                    \
    } while (0)

/* ---------------------------------------------------------------------- */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Decode hex into buf; returns byte count or (size_t)-1. */
static size_t hex_decode(const char *hex, uint8_t *buf, size_t buf_size)
{
    const size_t len = strlen(hex);
    if (len % 2 != 0 || len / 2 > buf_size)
        return (size_t)-1;
    for (size_t i = 0; i < len / 2; i++) {
        const int hi = hex_nibble(hex[2 * i]);
        const int lo = hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return (size_t)-1;
        buf[i] = (uint8_t)((hi << 4) | lo);
    }
    return len / 2;
}

/* NIST vectors: character index in this alphabet = numeral value. */
static const char NIST_ALPHABET[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static int nist_to_numerals(const char *s, uint8_t *out)
{
    const size_t len = strlen(s);
    for (size_t i = 0; i < len; i++) {
        const char *p = strchr(NIST_ALPHABET, s[i]);
        if (p == NULL || s[i] == '\0')
            return -1;
        out[i] = (uint8_t)(p - NIST_ALPHABET);
    }
    return (int)len;
}

static void test_nist_ff1(void)
{
    for (size_t i = 0; i < TV_NIST_COUNT; i++) {
        const tv_nist_t *tv = &TV_NIST[i];
        uint8_t key[32], tweak[64];
        const size_t key_len = hex_decode(tv->key_hex, key, sizeof key);
        const size_t tweak_len = hex_decode(tv->tweak_hex, tweak, sizeof tweak);
        CHECK(key_len != (size_t)-1 && tweak_len != (size_t)-1,
              "nist %d: bad hex in vector file", tv->sample);

        uint8_t pt[64], ct[64], out[64];
        const int n = nist_to_numerals(tv->plaintext, pt);
        const int n2 = nist_to_numerals(tv->ciphertext, ct);
        CHECK(n > 0 && n == n2, "nist %d: bad numeral strings", tv->sample);

        dealcode_ff1_t ff1;
        dealcode_err_t err = dealcode_ff1_init(&ff1, key, key_len, tv->radix);
        CHECK(err == DEALCODE_OK, "nist %d: ff1 init: %s", tv->sample,
              dealcode_strerror(err));

        err = dealcode_ff1_encrypt(&ff1, tweak, tweak_len, pt, (size_t)n, out);
        CHECK(err == DEALCODE_OK, "nist %d: encrypt: %s", tv->sample,
              dealcode_strerror(err));
        CHECK(memcmp(out, ct, (size_t)n) == 0,
              "nist %d: ciphertext mismatch", tv->sample);

        err = dealcode_ff1_decrypt(&ff1, tweak, tweak_len, ct, (size_t)n, out);
        CHECK(err == DEALCODE_OK, "nist %d: decrypt: %s", tv->sample,
              dealcode_strerror(err));
        CHECK(memcmp(out, pt, (size_t)n) == 0,
              "nist %d: plaintext mismatch", tv->sample);

        /* encrypt in place (x aliasing out) must work too */
        memcpy(out, pt, (size_t)n);
        err = dealcode_ff1_encrypt(&ff1, tweak, tweak_len, out, (size_t)n, out);
        CHECK(err == DEALCODE_OK && memcmp(out, ct, (size_t)n) == 0,
              "nist %d: in-place encrypt mismatch", tv->sample);
    }
}

/* ---------------------------------------------------------------------- */

static dealcode_t *make_codec(const tv_config_t *cfg, dealcode_err_t *err_out)
{
    uint8_t key[64];
    dealcode_config_t c = { 0 };
    if (cfg->key_hex != NULL) {
        const size_t key_len = hex_decode(cfg->key_hex, key, sizeof key);
        if (key_len == (size_t)-1) {
            *err_out = DEALCODE_ERR_CONFIG;
            return NULL;
        }
        c.key = key;
        c.key_len = key_len;
    } else {
        c.key_string = cfg->key_string;
    }
    c.alphabet = cfg->alphabet;
    c.min_length = cfg->min_length;
    c.max_length = cfg->max_length;
    c.domain = cfg->domain;

    dealcode_t *dc = NULL;
    *err_out = dealcode_new(&c, &dc);
    return dc;
}

static void test_v1_vectors(void)
{
    for (size_t i = 0; i < TV_V1_COUNT; i++) {
        const tv_config_t *cfg = &TV_V1[i];
        dealcode_err_t err;
        dealcode_t *dc = make_codec(cfg, &err);
        CHECK(dc != NULL, "v1 %s: dealcode_new: %s", cfg->name,
              dealcode_strerror(err));
        if (dc == NULL)
            continue;

        CHECK(dealcode_min_length(dc) == cfg->min_length &&
                  dealcode_max_length(dc) == cfg->max_length,
              "v1 %s: length accessors", cfg->name);
        CHECK((size_t)dealcode_radix(dc) == strlen(dealcode_alphabet(dc)),
              "v1 %s: radix/alphabet accessors", cfg->name);

        for (size_t j = 0; j < cfg->n_vectors; j++) {
            const tv_pair_t *tv = &cfg->vectors[j];
            char code[DEALCODE_MAX_CODE_SIZE];
            err = dealcode_encode(dc, tv->n, code, sizeof code);
            CHECK(err == DEALCODE_OK, "v1 %s: encode(%" PRIu64 "): %s",
                  cfg->name, tv->n, dealcode_strerror(err));
            CHECK(err == DEALCODE_OK && strcmp(code, tv->code) == 0,
                  "v1 %s: encode(%" PRIu64 ") = \"%s\", want \"%s\"",
                  cfg->name, tv->n, code, tv->code);

            uint64_t n = UINT64_MAX;
            err = dealcode_decode(dc, tv->code, &n);
            CHECK(err == DEALCODE_OK && n == tv->n,
                  "v1 %s: decode(\"%s\") = %" PRIu64 " (%s), want %" PRIu64,
                  cfg->name, tv->code, n, dealcode_strerror(err), tv->n);
        }

        for (size_t j = 0; j < cfg->n_invalid_codes; j++) {
            uint64_t n = 0;
            err = dealcode_decode(dc, cfg->invalid_codes[j], &n);
            CHECK(err == DEALCODE_ERR_INVALID_CODE,
                  "v1 %s: decode(\"%s\") = %s, want invalid-code",
                  cfg->name, cfg->invalid_codes[j], dealcode_strerror(err));
        }

        for (size_t j = 0; j < cfg->n_normalize; j++) {
            uint64_t n = UINT64_MAX;
            err = dealcode_decode(dc, cfg->normalize[j].input, &n);
            CHECK(err == DEALCODE_OK && n == cfg->normalize[j].n,
                  "v1 %s: normalize decode(\"%s\") = %" PRIu64
                  " (%s), want %" PRIu64,
                  cfg->name, cfg->normalize[j].input, n,
                  dealcode_strerror(err), cfg->normalize[j].n);
        }

        for (size_t j = 0; j < cfg->n_range_counters; j++) {
            char code[DEALCODE_MAX_CODE_SIZE];
            err = dealcode_encode(dc, cfg->range_counters[j], code,
                                  sizeof code);
            CHECK(err == DEALCODE_ERR_RANGE,
                  "v1 %s: encode(%" PRIu64 ") = %s, want range error",
                  cfg->name, cfg->range_counters[j], dealcode_strerror(err));
        }

        dealcode_free(dc);
    }
}

static void test_v1_invalid_configs(void)
{
    for (size_t i = 0; i < TV_V1_INVALID_CONFIG_COUNT; i++) {
        const tv_invalid_config_t *tv = &TV_V1_INVALID_CONFIGS[i];
        uint8_t key[64];
        dealcode_config_t c = { 0 };
        if (tv->key_hex != NULL) {
            const size_t key_len = hex_decode(tv->key_hex, key, sizeof key);
            CHECK(key_len != (size_t)-1, "invalid-config %s: bad key hex",
                  tv->name);
            if (key_len == (size_t)-1)
                continue;
            c.key = key;
            c.key_len = key_len;
        } else {
            c.key_string = tv->key_string;
        }
        c.alphabet = tv->alphabet;
        c.min_length = tv->min_length;
        c.max_length = tv->max_length;
        c.domain = tv->domain;

        dealcode_t *dc = NULL;
        dealcode_err_t err = dealcode_new(&c, &dc);
        CHECK(err == DEALCODE_ERR_CONFIG && dc == NULL,
              "invalid-config %s: got %s, want config error", tv->name,
              dealcode_strerror(err));

        /* the _ex form must agree, and must explain itself */
        char errbuf[DEALCODE_ERRBUF_SIZE];
        err = dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf);
        CHECK(err == DEALCODE_ERR_CONFIG && dc == NULL,
              "invalid-config %s: _ex got %s, want config error", tv->name,
              dealcode_strerror(err));
        CHECK(errbuf[0] != '\0',
              "invalid-config %s: _ex must write a diagnostic", tv->name);
        dealcode_free(dc);
    }
}

/* ---------------------------------------------------------------------- */

static void test_buffer_errors(void)
{
    dealcode_config_t c = { 0 };
    c.key_string = "buffer-test-key";
    dealcode_t *dc = NULL;
    dealcode_err_t err = dealcode_new(&c, &dc);
    CHECK(err == DEALCODE_OK, "buffer: dealcode_new: %s",
          dealcode_strerror(err));
    if (dc == NULL)
        return;

    /* defaults: hex, min_length 6 -> code for 0 needs 7 bytes */
    char code[DEALCODE_MAX_CODE_SIZE];
    memset(code, 'Z', sizeof code);
    CHECK(dealcode_encode(dc, 0, code, 6) == DEALCODE_ERR_BUFFER,
          "buffer: size 6 must be too small for a 6-char code");
    CHECK(code[0] == 'Z', "buffer: nothing may be written on ERR_BUFFER");
    CHECK(dealcode_encode(dc, 0, code, 0) == DEALCODE_ERR_BUFFER,
          "buffer: size 0");
    CHECK(dealcode_encode(dc, 0, NULL, 64) == DEALCODE_ERR_BUFFER,
          "buffer: NULL out");
    CHECK(dealcode_encode(dc, 0, code, 7) == DEALCODE_OK &&
              strlen(code) == 6,
          "buffer: exact size must succeed");

    /* a counter in stage 7 needs 8 bytes */
    CHECK(dealcode_encode(dc, UINT64_C(16777216), code, 7) ==
              DEALCODE_ERR_BUFFER,
          "buffer: stage-7 code needs 8 bytes");
    CHECK(dealcode_encode(dc, UINT64_C(16777216), code, 8) == DEALCODE_OK,
          "buffer: stage-7 exact size");
    dealcode_free(dc);
}

static void test_range_errors(void)
{
    dealcode_config_t c = { 0 };
    c.key_string = "range-test-key";
    c.alphabet = "dec";
    c.min_length = 4;
    c.max_length = 6;
    dealcode_t *dc = NULL;
    CHECK(dealcode_new(&c, &dc) == DEALCODE_OK, "range: dealcode_new");
    if (dc == NULL)
        return;
    CHECK(dealcode_capacity(dc) == UINT64_C(1000000), "range: capacity 10^6");

    char code[DEALCODE_MAX_CODE_SIZE];
    CHECK(dealcode_encode(dc, UINT64_C(999999), code, sizeof code) ==
              DEALCODE_OK,
          "range: capacity-1 encodes");
    CHECK(dealcode_encode(dc, UINT64_C(1000000), code, sizeof code) ==
              DEALCODE_ERR_RANGE,
          "range: capacity is rejected");
    CHECK(dealcode_encode(dc, UINT64_MAX, code, sizeof code) ==
              DEALCODE_ERR_RANGE,
          "range: UINT64_MAX is rejected");
    dealcode_free(dc);
}

static void expect_config_error(const dealcode_config_t *cfg, const char *what)
{
    dealcode_t *dc = NULL;
    const dealcode_err_t err = dealcode_new(cfg, &dc);
    CHECK(err == DEALCODE_ERR_CONFIG && dc == NULL,
          "config: %s: got %s", what, dealcode_strerror(err));
    dealcode_free(dc);
}

static void test_config_errors(void)
{
    static const uint8_t key16[16] = { 0 };
    dealcode_config_t c;

    memset(&c, 0, sizeof c);
    expect_config_error(&c, "no key at all");

    memset(&c, 0, sizeof c);
    c.key_string = "";
    expect_config_error(&c, "empty key string");

    memset(&c, 0, sizeof c);
    c.key = key16;
    c.key_len = 0;
    expect_config_error(&c, "empty key bytes");

    memset(&c, 0, sizeof c);
    c.key = key16;
    c.key_len = 16;
    c.key_string = "also-a-key";
    expect_config_error(&c, "both key and key_string");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "x"; /* 1 char: too short for a custom alphabet */
    expect_config_error(&c, "custom alphabet too short");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "abca"; /* duplicate characters */
    expect_config_error(&c, "custom alphabet duplicate chars");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "ab cd"; /* space is not in 0x21-0x7E */
    expect_config_error(&c, "custom alphabet with space");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "ab\x7f"
                 "cd"; /* DEL is not printable */
    expect_config_error(&c, "custom alphabet with DEL");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.min_length = 1;
    expect_config_error(&c, "min_length < 2");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.min_length = -3;
    expect_config_error(&c, "negative min_length");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "01"; /* radix 2 */
    c.min_length = 6;  /* 2^6 = 64 < 100 */
    expect_config_error(&c, "radix^min_length < 100");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.min_length = 8;
    c.max_length = 7;
    expect_config_error(&c, "max_length < min_length");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.max_length = 33; /* hex: 16^33 = 2^132 > 2^128 */
    expect_config_error(&c, "radix^max_length > 2^128");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "01";
    c.min_length = 7;
    c.max_length = 129; /* > 128 is impossible for any radix */
    expect_config_error(&c, "max_length > 128");

    {
        static char domain[300];
        memset(domain, 'a', 256);
        domain[256] = '\0';
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        c.domain = domain;
        expect_config_error(&c, "domain > 255 bytes");
    }

    /* NULL argument handling */
    {
        dealcode_t *dc = NULL;
        CHECK(dealcode_new(NULL, &dc) == DEALCODE_ERR_CONFIG,
              "config: NULL cfg");
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        CHECK(dealcode_new(&c, NULL) == DEALCODE_ERR_CONFIG,
              "config: NULL out");
    }

    /* boundary cases that must SUCCEED */
    {
        dealcode_t *dc = NULL;
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        c.max_length = 32; /* 16^32 == 2^128 exactly: allowed */
        CHECK(dealcode_new(&c, &dc) == DEALCODE_OK,
              "config: radix^max_length == 2^128 must be accepted");
        CHECK(dealcode_capacity(dc) == ((uint64_t)1 << 63),
              "config: capacity capped at 2^63");
        dealcode_free(dc);

        memset(&c, 0, sizeof c);
        c.key_string = "k";
        c.alphabet = "dec";
        c.min_length = 2; /* 10^2 = 100 >= 100: minimum FF1 domain */
        CHECK(dealcode_new(&c, &dc) == DEALCODE_OK,
              "config: radix^min_length == 100 must be accepted");
        dealcode_free(dc);
    }
}

/* Guard A (SPEC.md §3.2): a custom alphabet that is not exactly a preset
 * name but ASCII-case-insensitively equals one must be rejected; the exact
 * preset name keeps resolving as the preset. */
static void test_preset_name_alphabet_guard(void)
{
    static const char *const disguised[] = {
        "DEC",  "Dec",  "HEX",       "Hex",       "hEx",
        "BASE32", "Base32", "CROCKFORD", "Crockford",
        "BASE36", "Base36", "BASE58", "Base58",
        "BASE62", "Base62", "BASE64URL", "Base64Url", "base64URL",
    };
    dealcode_config_t c;
    for (size_t i = 0; i < sizeof disguised / sizeof disguised[0]; i++) {
        memset(&c, 0, sizeof c);
        c.key_string = "guard-key";
        c.alphabet = disguised[i];
        dealcode_t *dc = NULL;
        CHECK(dealcode_new(&c, &dc) == DEALCODE_ERR_CONFIG && dc == NULL,
              "guard A: alphabet \"%s\" must be rejected", disguised[i]);
        dealcode_free(dc);
    }

    /* exact preset names still resolve as presets */
    memset(&c, 0, sizeof c);
    c.key_string = "guard-key";
    c.alphabet = "hex";
    dealcode_t *dc = NULL;
    CHECK(dealcode_new(&c, &dc) == DEALCODE_OK, "guard A: exact \"hex\" ok");
    if (dc != NULL)
        CHECK(strcmp(dealcode_alphabet(dc), "0123456789abcdef") == 0,
              "guard A: \"hex\" resolves as the preset");
    dealcode_free(dc);
    dc = NULL;

    /* genuinely custom alphabets that merely resemble names still work */
    static const char *const genuine[] = { "HEXA", "xeh", "dce", "based" };
    for (size_t i = 0; i < sizeof genuine / sizeof genuine[0]; i++) {
        memset(&c, 0, sizeof c);
        c.key_string = "guard-key";
        c.alphabet = genuine[i];
        CHECK(dealcode_new(&c, &dc) == DEALCODE_OK,
              "guard A: custom \"%s\" must be accepted", genuine[i]);
        if (dc != NULL)
            CHECK(strcmp(dealcode_alphabet(dc), genuine[i]) == 0,
                  "guard A: custom \"%s\" is used verbatim", genuine[i]);
        dealcode_free(dc);
        dc = NULL;
    }

    /* exact diagnostic wording */
    char errbuf[DEALCODE_ERRBUF_SIZE];
    memset(&c, 0, sizeof c);
    c.key_string = "guard-key";
    c.alphabet = "HEX";
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
              DEALCODE_ERR_CONFIG,
          "guard A: _ex rejects \"HEX\"");
    CHECK(strcmp(errbuf,
                 "custom alphabet \"HEX\" matches the preset name \"hex\" — "
                 "pass \"hex\" for the preset, or a genuinely custom "
                 "alphabet") == 0,
          "guard A: diagnostic, got \"%s\"", errbuf);
}

/* Guard B (SPEC.md §2.1): a STRING key that ASCII-case-insensitively equals
 * a preset alphabet name must be rejected (swapped-arguments trap). Byte
 * keys are unaffected. */
static void test_preset_name_key_guard(void)
{
    static const char *const names[] = {
        "dec", "hex", "base32", "crockford", "base36", "base58",
        "base62", "base64url", "HEX", "Crockford", "BASE64url",
    };
    dealcode_config_t c;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        memset(&c, 0, sizeof c);
        c.key_string = names[i];
        dealcode_t *dc = NULL;
        CHECK(dealcode_new(&c, &dc) == DEALCODE_ERR_CONFIG && dc == NULL,
              "guard B: string key \"%s\" must be rejected", names[i]);
        dealcode_free(dc);
    }

    /* near-misses are fine */
    static const char *const fine[] = { "crockford1", "hex ", " hex",
                                        "base-62", "hexhex" };
    for (size_t i = 0; i < sizeof fine / sizeof fine[0]; i++) {
        memset(&c, 0, sizeof c);
        c.key_string = fine[i];
        dealcode_t *dc = NULL;
        CHECK(dealcode_new(&c, &dc) == DEALCODE_OK,
              "guard B: string key \"%s\" must be accepted", fine[i]);
        dealcode_free(dc);
    }

    /* BYTE keys spelling a preset name are unaffected */
    memset(&c, 0, sizeof c);
    c.key = (const uint8_t *)"crockford";
    c.key_len = 9;
    dealcode_t *dc = NULL;
    CHECK(dealcode_new(&c, &dc) == DEALCODE_OK,
          "guard B: byte key \"crockford\" must be accepted");
    dealcode_free(dc);
    dc = NULL;

    /* exact diagnostic wording (echoing the key is deliberate here: a
     * preset name is not a secret) */
    char errbuf[DEALCODE_ERRBUF_SIZE];
    memset(&c, 0, sizeof c);
    c.key_string = "crockford";
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
              DEALCODE_ERR_CONFIG,
          "guard B: _ex rejects \"crockford\"");
    CHECK(strcmp(errbuf,
                 "string key \"crockford\" is a preset alphabet name — did "
                 "you swap the key and alphabet fields?") == 0,
          "guard B: diagnostic, got \"%s\"", errbuf);
}

/* dealcode_new_ex: diagnostic channel behaviour. */
static void test_new_ex_details(void)
{
    dealcode_config_t c;
    dealcode_t *dc = NULL;
    char errbuf[DEALCODE_ERRBUF_SIZE];

    /* success: OK, handle set, errbuf cleared to "" */
    memset(&c, 0, sizeof c);
    c.key_string = "detail-key";
    memset(errbuf, 'x', sizeof errbuf);
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) == DEALCODE_OK &&
              dc != NULL,
          "new_ex: success");
    CHECK(errbuf[0] == '\0', "new_ex: errbuf empty on success");
    dealcode_free(dc);
    dc = NULL;

    /* NULL errbuf / zero length are fine */
    memset(&c, 0, sizeof c);
    c.key_string = "";
    CHECK(dealcode_new_ex(&c, &dc, NULL, 0) == DEALCODE_ERR_CONFIG,
          "new_ex: NULL errbuf tolerated");
    CHECK(dealcode_new_ex(&c, &dc, errbuf, 0) == DEALCODE_ERR_CONFIG,
          "new_ex: zero-length errbuf tolerated");

    /* specific messages */
    memset(&c, 0, sizeof c);
    c.key_string = "";
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "key: empty") == 0,
          "new_ex: empty key message, got \"%s\"", errbuf);

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "abca";
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "alphabet: duplicate character 'a'") == 0,
          "new_ex: duplicate char message, got \"%s\"", errbuf);

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.min_length = 1;
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "min_length 1 < 2") == 0,
          "new_ex: min_length message, got \"%s\"", errbuf);

    {
        static char domain[300];
        memset(domain, 'a', 256);
        domain[256] = '\0';
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        c.domain = domain;
        CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                      DEALCODE_ERR_CONFIG &&
                  strcmp(errbuf,
                         "domain exceeds 255 UTF-8 bytes (got 256)") == 0,
              "new_ex: domain message, got \"%s\"", errbuf);
    }

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.min_length = 8;
    c.max_length = 7;
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "max_length 7 < min_length 8") == 0,
          "new_ex: max<min message, got \"%s\"", errbuf);

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.max_length = 33;
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "radix^max_length (16^33) exceeds 2^128") == 0,
          "new_ex: max_length overflow message, got \"%s\"", errbuf);

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "01";
    c.min_length = 6;
    CHECK(dealcode_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "radix^min_length (2^6) is below FF1's minimum "
                             "code space of 100") == 0,
          "new_ex: small code space message, got \"%s\"", errbuf);

    /* truncation: message cut to fit, still NUL-terminated */
    char tiny[8];
    memset(&c, 0, sizeof c);
    c.key_string = "";
    CHECK(dealcode_new_ex(&c, &dc, tiny, sizeof tiny) ==
                  DEALCODE_ERR_CONFIG &&
              strlen(tiny) == sizeof tiny - 1 &&
              strncmp(tiny, "key: em", sizeof tiny - 1) == 0,
          "new_ex: truncated message, got \"%s\"", tiny);
}

static void test_defaults(void)
{
    /* default max_length: largest L with radix^L <= 2^63 - 1 */
    static const struct {
        const char *alphabet;
        int expect_max;
    } cases[] = {
        { "hex", 15 },      { "dec", 18 },    { "base32", 12 },
        { "crockford", 12 }, { "base36", 12 }, { "base58", 10 },
        { "base62", 10 },   { "base64url", 10 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        dealcode_config_t c = { 0 };
        c.key_string = "defaults-key";
        c.alphabet = cases[i].alphabet;
        dealcode_t *dc = NULL;
        CHECK(dealcode_new(&c, &dc) == DEALCODE_OK, "defaults: %s",
              cases[i].alphabet);
        if (dc == NULL)
            continue;
        CHECK(dealcode_min_length(dc) == 6, "defaults: %s min_length 6",
              cases[i].alphabet);
        CHECK(dealcode_max_length(dc) == cases[i].expect_max,
              "defaults: %s max_length %d, want %d", cases[i].alphabet,
              dealcode_max_length(dc), cases[i].expect_max);
        dealcode_free(dc);
    }

    /* NULL alphabet means "hex"; NULL domain means "" */
    dealcode_config_t c = { 0 };
    c.key_string = "defaults-key";
    dealcode_t *dc_null = NULL, *dc_hex = NULL;
    CHECK(dealcode_new(&c, &dc_null) == DEALCODE_OK, "defaults: NULL alphabet");
    c.alphabet = "hex";
    c.domain = "";
    CHECK(dealcode_new(&c, &dc_hex) == DEALCODE_OK, "defaults: explicit hex");
    if (dc_null != NULL && dc_hex != NULL) {
        CHECK(strcmp(dealcode_alphabet(dc_null), "0123456789abcdef") == 0,
              "defaults: alphabet accessor");
        char a[DEALCODE_MAX_CODE_SIZE], b[DEALCODE_MAX_CODE_SIZE];
        CHECK(dealcode_encode(dc_null, 12345, a, sizeof a) == DEALCODE_OK &&
                  dealcode_encode(dc_hex, 12345, b, sizeof b) == DEALCODE_OK &&
                  strcmp(a, b) == 0,
              "defaults: NULL alphabet/domain match explicit ones");
    }
    dealcode_free(dc_null);
    dealcode_free(dc_hex);
}

/* ---------------------------------------------------------------------- */

static void roundtrip_one(dealcode_t *dc, const char *name, uint64_t n,
                          int expect_len)
{
    char code[DEALCODE_MAX_CODE_SIZE];
    dealcode_err_t err = dealcode_encode(dc, n, code, sizeof code);
    CHECK(err == DEALCODE_OK, "roundtrip %s: encode(%" PRIu64 "): %s", name, n,
          dealcode_strerror(err));
    if (err != DEALCODE_OK)
        return;
    if (expect_len > 0)
        CHECK((int)strlen(code) == expect_len,
              "roundtrip %s: encode(%" PRIu64 ") length %zu, want %d", name, n,
              strlen(code), expect_len);
    uint64_t back = UINT64_MAX;
    err = dealcode_decode(dc, code, &back);
    CHECK(err == DEALCODE_OK && back == n,
          "roundtrip %s: decode(encode(%" PRIu64 ")) = %" PRIu64 " (%s)", name,
          n, back, dealcode_strerror(err));
}

/* Exhaustive-ish roundtrips: a dense range plus every stage boundary. */
static void roundtrip_config(const char *name, const char *alphabet,
                             int min_length, int max_length,
                             const char *domain)
{
    dealcode_config_t c = { 0 };
    c.key_string = "roundtrip-key";
    c.alphabet = alphabet;
    c.min_length = min_length;
    c.max_length = max_length;
    c.domain = domain;
    dealcode_t *dc = NULL;
    dealcode_err_t err = dealcode_new(&c, &dc);
    CHECK(err == DEALCODE_OK, "roundtrip %s: new: %s", name,
          dealcode_strerror(err));
    if (dc == NULL)
        return;

    const uint64_t capacity = dealcode_capacity(dc);
    const uint64_t radix = (uint64_t)dealcode_radix(dc);
    const int min_len = dealcode_min_length(dc);
    const int max_len = dealcode_max_length(dc);

    /* dense low range */
    uint64_t dense = capacity < 3000 ? capacity : 3000;
    for (uint64_t n = 0; n < dense; n++)
        roundtrip_one(dc, name, n, 0);

    /* stage boundaries: around radix^d for every d, plus capacity-1 */
    uint64_t power = 1; /* radix^d, while it fits and d <= max_len */
    int d = 0;
    while (d < max_len) {
        if (power > capacity / radix + 1)
            break;
        power *= radix;
        d++;
        if (d < min_len)
            continue;
        const int len_below = d < min_len ? min_len : d;
        const int len_at = (d + 1) < min_len ? min_len : (d + 1);
        if (power >= 2 && power - 2 < capacity)
            roundtrip_one(dc, name, power - 2, len_below);
        if (power - 1 < capacity)
            roundtrip_one(dc, name, power - 1, len_below);
        if (d < max_len) {
            if (power < capacity)
                roundtrip_one(dc, name, power, len_at);
            if (power + 1 < capacity)
                roundtrip_one(dc, name, power + 1, len_at);
        }
    }
    roundtrip_one(dc, name, capacity - 1, 0);
    roundtrip_one(dc, name, capacity / 2, 0);

    dealcode_free(dc);
}

static void test_roundtrips(void)
{
    roundtrip_config("hex-default", "hex", 0, 0, NULL);
    roundtrip_config("dec-4-6", "dec", 4, 6, "pin");
    roundtrip_config("base62", "base62", 0, 0, "orders");
    roundtrip_config("crockford", "crockford", 0, 0, NULL);
    roundtrip_config("custom", "BCDFGHJKLMNPQRSTVWXZ", 6, 14, "custom");
    /* radix^max_length == 2^128 exactly, in two shapes: */
    roundtrip_config("hex-32-fixed", "hex", 32, 32, NULL);
    roundtrip_config("binary-7-128", "01", 7, 128, NULL);
    /* radix^max_length == 2^64 (> 2^63, not a u128 edge but a capacity cap) */
    roundtrip_config("hex-16-fixed", "hex", 16, 16, NULL);
}

static void test_counter_bound_rejection(void)
{
    /* With alphabet "01", max_length 128, stages above d=63 start at
     * base = 2^(d-1) >= 2^63, so ANY code longer than 64 chars must be
     * rejected by the counter-space check regardless of its decryption. */
    dealcode_config_t c = { 0 };
    c.key_string = "bound-key";
    c.alphabet = "01";
    c.min_length = 7;
    c.max_length = 128;
    dealcode_t *dc = NULL;
    CHECK(dealcode_new(&c, &dc) == DEALCODE_OK, "bound: new");
    if (dc == NULL)
        return;
    CHECK(dealcode_capacity(dc) == ((uint64_t)1 << 63),
          "bound: capacity 2^63");

    char code[DEALCODE_MAX_CODE_SIZE];
    for (size_t len = 65; len <= 128; len += 21) {
        memset(code, '0', len);
        code[len] = '\0';
        uint64_t n = 0;
        CHECK(dealcode_decode(dc, code, &n) == DEALCODE_ERR_INVALID_CODE,
              "bound: %zu-char code must be rejected", len);
        memset(code, '1', len);
        code[len] = '\0';
        CHECK(dealcode_decode(dc, code, &n) == DEALCODE_ERR_INVALID_CODE,
              "bound: %zu-char code must be rejected (ones)", len);
    }

    /* length limits */
    uint64_t n = 0;
    memset(code, '0', 6);
    code[6] = '\0';
    CHECK(dealcode_decode(dc, code, &n) == DEALCODE_ERR_INVALID_CODE,
          "bound: too-short code rejected");
    CHECK(dealcode_decode(dc, "", &n) == DEALCODE_ERR_INVALID_CODE,
          "bound: empty code rejected");
    dealcode_free(dc);
}

static void test_domain_separation(void)
{
    dealcode_config_t c = { 0 };
    c.key_string = "separation-key";
    dealcode_t *a = NULL, *b = NULL;
    c.domain = "orders";
    CHECK(dealcode_new(&c, &a) == DEALCODE_OK, "domain: new a");
    c.domain = "coupons";
    CHECK(dealcode_new(&c, &b) == DEALCODE_OK, "domain: new b");
    if (a != NULL && b != NULL) {
        int all_equal = 1;
        for (uint64_t n = 0; n < 32; n++) {
            char ca[DEALCODE_MAX_CODE_SIZE], cb[DEALCODE_MAX_CODE_SIZE];
            CHECK(dealcode_encode(a, n, ca, sizeof ca) == DEALCODE_OK &&
                      dealcode_encode(b, n, cb, sizeof cb) == DEALCODE_OK,
                  "domain: encode");
            if (strcmp(ca, cb) != 0)
                all_equal = 0;
        }
        CHECK(!all_equal, "domain: different domains must permute differently");
    }
    dealcode_free(a);
    dealcode_free(b);
}

static void test_strerror(void)
{
    const dealcode_err_t codes[] = {
        DEALCODE_OK,          DEALCODE_ERR_CONFIG, DEALCODE_ERR_RANGE,
        DEALCODE_ERR_INVALID_CODE, DEALCODE_ERR_BUFFER, DEALCODE_ERR_CRYPTO,
        DEALCODE_ERR_NOMEM,
    };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        CHECK(dealcode_strerror(codes[i]) != NULL &&
                  dealcode_strerror(codes[i])[0] != '\0',
              "strerror: code %d", (int)codes[i]);
    CHECK(dealcode_strerror((dealcode_err_t)999) != NULL,
          "strerror: unknown code");
}

static void test_null_handles(void)
{
    char code[8];
    uint64_t n;
    CHECK(dealcode_capacity(NULL) == 0, "null: capacity");
    CHECK(dealcode_min_length(NULL) == 0, "null: min_length");
    CHECK(dealcode_max_length(NULL) == 0, "null: max_length");
    CHECK(dealcode_radix(NULL) == 0, "null: radix");
    CHECK(dealcode_alphabet(NULL) == NULL, "null: alphabet");
    CHECK(dealcode_encode(NULL, 0, code, sizeof code) == DEALCODE_ERR_CONFIG,
          "null: encode");
    CHECK(dealcode_decode(NULL, "abc", &n) == DEALCODE_ERR_CONFIG,
          "null: decode");
    dealcode_free(NULL); /* must be a no-op */

    dealcode_config_t c = { 0 };
    c.key_string = "k";
    dealcode_t *dc = NULL;
    CHECK(dealcode_new(&c, &dc) == DEALCODE_OK, "null: new");
    if (dc != NULL) {
        CHECK(dealcode_decode(dc, NULL, &n) == DEALCODE_ERR_INVALID_CODE,
              "null: decode NULL code");
        CHECK(dealcode_decode(dc, "c4334d", NULL) == DEALCODE_ERR_CONFIG,
              "null: decode NULL n_out");
    }
    dealcode_free(dc);
}


/* ---------------------------------------------------------------------- */
/* Fixed-length cycling mode (SPEC section 11)                            */
/* ---------------------------------------------------------------------- */

static dealcode_cycle_t *make_cycle_codec(const tv_cycle_config_t *cfg,
                                          dealcode_err_t *err_out)
{
    uint8_t key[64];
    dealcode_cycle_config_t c = { 0 };
    if (cfg->key_hex != NULL) {
        const size_t key_len = hex_decode(cfg->key_hex, key, sizeof key);
        if (key_len == (size_t)-1) {
            *err_out = DEALCODE_ERR_CONFIG;
            return NULL;
        }
        c.key = key;
        c.key_len = key_len;
    } else {
        c.key_string = cfg->key_string;
    }
    c.alphabet = cfg->alphabet;
    c.length = cfg->length;
    c.domain = cfg->domain;

    dealcode_cycle_t *dc = NULL;
    *err_out = dealcode_cycle_new(&c, &dc);
    return dc;
}

static void test_v1c_vectors(void)
{
    for (size_t i = 0; i < TV_V1C_COUNT; i++) {
        const tv_cycle_config_t *cfg = &TV_V1C[i];
        dealcode_err_t err;
        dealcode_cycle_t *dc = make_cycle_codec(cfg, &err);
        CHECK(dc != NULL, "v1c %s: dealcode_cycle_new: %s", cfg->name,
              dealcode_strerror(err));
        if (dc == NULL)
            continue;

        CHECK(dealcode_cycle_capacity(dc) == cfg->capacity,
              "v1c %s: capacity %" PRIu64 ", want %" PRIu64, cfg->name,
              dealcode_cycle_capacity(dc), cfg->capacity);
        CHECK(dealcode_cycle_max_cycle(dc) == cfg->max_cycle,
              "v1c %s: max_cycle %" PRIu64 ", want %" PRIu64, cfg->name,
              dealcode_cycle_max_cycle(dc), cfg->max_cycle);
        CHECK(dealcode_cycle_length(dc) == cfg->length,
              "v1c %s: length accessor", cfg->name);
        CHECK((size_t)dealcode_cycle_radix(dc) ==
                  strlen(dealcode_cycle_alphabet(dc)),
              "v1c %s: radix/alphabet accessors", cfg->name);

        for (size_t j = 0; j < cfg->n_vectors; j++) {
            const tv_pair_t *tv = &cfg->vectors[j];
            char code[DEALCODE_MAX_CODE_SIZE];
            err = dealcode_cycle_encode(dc, tv->n, code, sizeof code);
            CHECK(err == DEALCODE_OK, "v1c %s: encode(%" PRIu64 "): %s",
                  cfg->name, tv->n, dealcode_strerror(err));
            CHECK(err == DEALCODE_OK && strcmp(code, tv->code) == 0,
                  "v1c %s: encode(%" PRIu64 ") = \"%s\", want \"%s\"",
                  cfg->name, tv->n, code, tv->code);

            uint64_t n = UINT64_MAX;
            err = dealcode_cycle_decode(dc, tv->code, tv->n / cfg->capacity,
                                        &n);
            CHECK(err == DEALCODE_OK && n == tv->n,
                  "v1c %s: decode(\"%s\", %" PRIu64 ") = %" PRIu64
                  " (%s), want %" PRIu64,
                  cfg->name, tv->code, tv->n / cfg->capacity, n,
                  dealcode_strerror(err), tv->n);
        }

        for (size_t j = 0; j < cfg->n_invalid_codes; j++) {
            uint64_t n = 0;
            err = dealcode_cycle_decode(dc, cfg->invalid_codes[j].code,
                                        cfg->invalid_codes[j].cycle, &n);
            CHECK(err == DEALCODE_ERR_INVALID_CODE,
                  "v1c %s: decode(\"%s\", %" PRIu64
                  ") = %s, want invalid-code",
                  cfg->name, cfg->invalid_codes[j].code,
                  cfg->invalid_codes[j].cycle, dealcode_strerror(err));
        }

        for (size_t j = 0; j < cfg->n_normalize; j++) {
            uint64_t n = UINT64_MAX;
            err = dealcode_cycle_decode(dc, cfg->normalize[j].input,
                                        cfg->normalize[j].cycle, &n);
            CHECK(err == DEALCODE_OK && n == cfg->normalize[j].n,
                  "v1c %s: normalize decode(\"%s\", %" PRIu64 ") = %" PRIu64
                  " (%s), want %" PRIu64,
                  cfg->name, cfg->normalize[j].input, cfg->normalize[j].cycle,
                  n, dealcode_strerror(err), cfg->normalize[j].n);
        }

        for (size_t j = 0; j < cfg->n_range_counters; j++) {
            char code[DEALCODE_MAX_CODE_SIZE];
            err = dealcode_cycle_encode(dc, cfg->range_counters[j], code,
                                        sizeof code);
            CHECK(err == DEALCODE_ERR_RANGE,
                  "v1c %s: encode(%" PRIu64 ") = %s, want range error",
                  cfg->name, cfg->range_counters[j], dealcode_strerror(err));
        }

        /* invalid cycles ("-1" is unrepresentable in uint64_t and was
         * skipped by the generator; max_cycle + 1 is always present) */
        char probe[DEALCODE_MAX_CODE_SIZE];
        CHECK(dealcode_cycle_encode(dc, 0, probe, sizeof probe) ==
                  DEALCODE_OK,
              "v1c %s: probe encode", cfg->name);
        for (size_t j = 0; j < cfg->n_invalid_cycles; j++) {
            uint64_t n = 0;
            err = dealcode_cycle_decode(dc, probe, cfg->invalid_cycles[j],
                                        &n);
            CHECK(err == DEALCODE_ERR_RANGE,
                  "v1c %s: decode(probe, %" PRIu64 ") = %s, want range error",
                  cfg->name, cfg->invalid_cycles[j], dealcode_strerror(err));
        }
        /* belt and braces: max_cycle + 1 must be rejected even if the
         * vector file changes (max_cycle < 2^63, so +1 never wraps) */
        {
            uint64_t n = 0;
            err = dealcode_cycle_decode(dc, probe,
                                        dealcode_cycle_max_cycle(dc) + 1, &n);
            CHECK(err == DEALCODE_ERR_RANGE,
                  "v1c %s: decode(probe, max_cycle + 1) = %s, want range "
                  "error",
                  cfg->name, dealcode_strerror(err));
        }

        dealcode_cycle_free(dc);
    }
}

static void test_v1c_invalid_configs(void)
{
    for (size_t i = 0; i < TV_V1C_INVALID_CONFIG_COUNT; i++) {
        const tv_cycle_invalid_config_t *tv = &TV_V1C_INVALID_CONFIGS[i];
        uint8_t key[64];
        dealcode_cycle_config_t c = { 0 };
        if (tv->key_hex != NULL) {
            const size_t key_len = hex_decode(tv->key_hex, key, sizeof key);
            CHECK(key_len != (size_t)-1, "v1c invalid-config %s: bad key hex",
                  tv->name);
            if (key_len == (size_t)-1)
                continue;
            c.key = key;
            c.key_len = key_len;
        } else {
            c.key_string = tv->key_string;
        }
        c.alphabet = tv->alphabet;
        c.length = tv->length;
        c.domain = tv->domain;

        dealcode_cycle_t *dc = NULL;
        dealcode_err_t err = dealcode_cycle_new(&c, &dc);
        CHECK(err == DEALCODE_ERR_CONFIG && dc == NULL,
              "v1c invalid-config %s: got %s, want config error", tv->name,
              dealcode_strerror(err));

        /* the _ex form must agree, and must explain itself */
        char errbuf[DEALCODE_ERRBUF_SIZE];
        err = dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf);
        CHECK(err == DEALCODE_ERR_CONFIG && dc == NULL,
              "v1c invalid-config %s: _ex got %s, want config error",
              tv->name, dealcode_strerror(err));
        CHECK(errbuf[0] != '\0',
              "v1c invalid-config %s: _ex must write a diagnostic", tv->name);
        dealcode_cycle_free(dc);
    }
}

/* The cycling constructor reuses the plain codec's guards verbatim, plus
 * its own length constraints; check the exact diagnostics. */
static void test_cycle_config_errors(void)
{
    dealcode_cycle_config_t c;
    dealcode_cycle_t *dc = NULL;
    char errbuf[DEALCODE_ERRBUF_SIZE];

    /* success: OK, handle set, errbuf cleared to "" */
    memset(&c, 0, sizeof c);
    c.key_string = "cycle-detail-key";
    memset(errbuf, 'x', sizeof errbuf);
    CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_OK &&
              dc != NULL,
          "cycle new_ex: success");
    CHECK(errbuf[0] == '\0', "cycle new_ex: errbuf empty on success");
    CHECK(dealcode_cycle_length(dc) == 6, "cycle: length 0 defaults to 6");
    dealcode_cycle_free(dc);
    dc = NULL;

    /* guard A: preset-name-in-disguise alphabet, same message as plain */
    memset(&c, 0, sizeof c);
    c.key_string = "guard-key";
    c.alphabet = "HEX";
    c.length = 6;
    CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              dc == NULL,
          "cycle guard A: \"HEX\" rejected");
    CHECK(strcmp(errbuf,
                 "custom alphabet \"HEX\" matches the preset name \"hex\" — "
                 "pass \"hex\" for the preset, or a genuinely custom "
                 "alphabet") == 0,
          "cycle guard A: diagnostic, got \"%s\"", errbuf);

    /* guard B: preset-name string key, same message as plain */
    memset(&c, 0, sizeof c);
    c.key_string = "crockford";
    c.length = 6;
    CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              dc == NULL,
          "cycle guard B: string key \"crockford\" rejected");
    CHECK(strcmp(errbuf,
                 "string key \"crockford\" is a preset alphabet name — did "
                 "you swap the key and alphabet fields?") == 0,
          "cycle guard B: diagnostic, got \"%s\"", errbuf);

    /* byte keys spelling a preset name are unaffected (as in plain) */
    memset(&c, 0, sizeof c);
    c.key = (const uint8_t *)"crockford";
    c.key_len = 9;
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK,
          "cycle guard B: byte key \"crockford\" accepted");
    dealcode_cycle_free(dc);
    dc = NULL;

    /* length constraints, checked before any power */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.length = 1;
    CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "length 1 < 2") == 0,
          "cycle: length 1 message, got \"%s\"", errbuf);

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.length = -7;
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "cycle: negative length rejected");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.length = 129;
    CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "length 129 > 128") == 0,
          "cycle: length 129 message, got \"%s\"", errbuf);

    /* radix^length < 100 */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "abcdefghi"; /* radix 9: 9^2 = 81 < 100 */
    c.length = 2;
    CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "radix^length (9^2) is below FF1's minimum "
                             "code space of 100") == 0,
          "cycle: small code space message, got \"%s\"", errbuf);

    /* radix^length > 2^63 (16^16 = 2^64) */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.length = 16;
    CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf,
                     "radix^length (16^16) exceeds 2^63 in cycling mode — a "
                     "cycle must be completable; use min_length == "
                     "max_length for larger fixed spaces") == 0,
          "cycle: over-2^63 message, got \"%s\"", errbuf);

    /* oversized domain: same message as plain */
    {
        static char domain[300];
        memset(domain, 'a', 256);
        domain[256] = '\0';
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        c.domain = domain;
        CHECK(dealcode_cycle_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                      DEALCODE_ERR_CONFIG &&
                  strcmp(errbuf,
                         "domain exceeds 255 UTF-8 bytes (got 256)") == 0,
              "cycle: domain message, got \"%s\"", errbuf);
    }

    /* invalid UTF-8 domain and key, as in plain */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.domain = "\xff\xfe";
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "cycle: invalid UTF-8 domain rejected");
    memset(&c, 0, sizeof c);
    c.key_string = "\xc0\xaf";
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "cycle: invalid UTF-8 key string rejected");

    /* boundary that must SUCCEED: radix^length == 2^63 exactly */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "01234567";
    c.length = 21; /* 8^21 == 2^63 */
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK,
          "cycle: radix^length == 2^63 must be accepted");
    if (dc != NULL) {
        CHECK(dealcode_cycle_capacity(dc) == (UINT64_C(1) << 63),
              "cycle: capacity == 2^63");
        CHECK(dealcode_cycle_max_cycle(dc) == 0, "cycle: max_cycle == 0");
    }
    dealcode_cycle_free(dc);
    dc = NULL;

    /* boundary that must SUCCEED: radix^length == 100 exactly */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.alphabet = "dec";
    c.length = 2;
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK,
          "cycle: radix^length == 100 must be accepted");
    dealcode_cycle_free(dc);

    /* NULL argument handling */
    {
        dealcode_cycle_t *out = NULL;
        CHECK(dealcode_cycle_new(NULL, &out) == DEALCODE_ERR_CONFIG,
              "cycle: NULL cfg");
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        CHECK(dealcode_cycle_new(&c, NULL) == DEALCODE_ERR_CONFIG,
              "cycle: NULL out");
    }
}

/* SPEC section 11.3 behaviour: within one cycle codes are a permutation of
 * the whole fixed-length space; across cycles the same strings recur in a
 * different order. */
static void test_cycle_permutation_behaviour(void)
{
    dealcode_cycle_config_t c = { 0 };
    c.key_string = "cycle-behaviour-key";
    c.alphabet = "dec";
    c.length = 2; /* capacity 100 */
    dealcode_cycle_t *dc = NULL;
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK, "perm: new");
    if (dc == NULL)
        return;
    CHECK(dealcode_cycle_capacity(dc) == 100, "perm: capacity 100");

    /* codes[e][v]: the full 100-code set of cycles 0, 1, 2 */
    static char codes[3][100][3];
    for (uint64_t e = 0; e < 3; e++) {
        int seen[100] = { 0 };
        for (uint64_t v = 0; v < 100; v++) {
            const uint64_t n = e * 100 + v;
            CHECK(dealcode_cycle_encode(dc, n, codes[e][v],
                                        sizeof codes[e][v]) == DEALCODE_OK,
                  "perm: encode(%" PRIu64 ")", n);
            /* each 2-char dec code maps to one slot in [0, 100) */
            const int slot = (codes[e][v][0] - '0') * 10 +
                             (codes[e][v][1] - '0');
            CHECK(slot >= 0 && slot < 100 && !seen[slot],
                  "perm: cycle %" PRIu64 " must issue distinct codes", e);
            seen[slot] = 1;

            uint64_t back = UINT64_MAX;
            CHECK(dealcode_cycle_decode(dc, codes[e][v], e, &back) ==
                          DEALCODE_OK &&
                      back == n,
                  "perm: decode(\"%s\", %" PRIu64 ")", codes[e][v], e);
        }
        /* all 100 slots seen => this cycle covered the full space */
        int covered = 1;
        for (int s = 0; s < 100; s++)
            covered = covered && seen[s];
        CHECK(covered, "perm: cycle %" PRIu64 " covers the full space", e);
    }

    /* same set every cycle (checked via full coverage above), different
     * order between any two cycles */
    int diff01 = 0, diff12 = 0, diff02 = 0;
    for (int v = 0; v < 100; v++) {
        diff01 = diff01 || strcmp(codes[0][v], codes[1][v]) != 0;
        diff12 = diff12 || strcmp(codes[1][v], codes[2][v]) != 0;
        diff02 = diff02 || strcmp(codes[0][v], codes[2][v]) != 0;
    }
    CHECK(diff01 && diff12 && diff02,
          "perm: cycles must refill the space in different orders");

    /* wrong cycle: decodes fine (same charset/length) but to a different
     * counter — the documented ambiguity; cycle is context */
    uint64_t back = UINT64_MAX;
    CHECK(dealcode_cycle_decode(dc, codes[0][7], 0, &back) == DEALCODE_OK &&
              back == 7,
          "perm: right cycle");
    CHECK(dealcode_cycle_decode(dc, codes[0][7], 1, &back) == DEALCODE_OK &&
              back != 7 && back >= 100 && back < 200,
          "perm: wrong cycle decodes into that cycle's counter range");

    dealcode_cycle_free(dc);
}

static void test_cycle_boundaries(void)
{
    const uint64_t top = ((UINT64_C(1) << 63) - 1);

    /* dec length 2: counter-space top lives in the final partial cycle */
    {
        dealcode_cycle_config_t c = { 0 };
        c.key_string = "cycle-boundary-key";
        c.alphabet = "dec";
        c.length = 2;
        dealcode_cycle_t *dc = NULL;
        CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK, "boundary: new");
        if (dc == NULL)
            return;

        char code[DEALCODE_MAX_CODE_SIZE];
        CHECK(dealcode_cycle_encode(dc, top, code, sizeof code) ==
                  DEALCODE_OK,
              "boundary: encode(2^63 - 1)");
        uint64_t back = 0;
        CHECK(dealcode_cycle_decode(dc, code, top / 100, &back) ==
                      DEALCODE_OK &&
                  back == top,
              "boundary: 2^63 - 1 roundtrip");
        CHECK(dealcode_cycle_encode(dc, UINT64_C(1) << 63, code,
                                    sizeof code) == DEALCODE_ERR_RANGE,
              "boundary: encode(2^63) rejected");
        CHECK(dealcode_cycle_encode(dc, UINT64_MAX, code, sizeof code) ==
                  DEALCODE_ERR_RANGE,
              "boundary: encode(UINT64_MAX) rejected");

        /* final partial cycle: max_cycle holds only counters up to top; the
         * other codes of that cycle must be rejected. 2^63 - 1 = ...07, so
         * v in [8, 100) of cycle max_cycle maps past the counter space:
         * every code except the 8 valid ones must be invalid. */
        const uint64_t last = dealcode_cycle_max_cycle(dc);
        int rejected = 0, accepted = 0;
        for (int d1 = 0; d1 < 10 && rejected < 3; d1++) {
            for (int d2 = 0; d2 < 10 && rejected < 3; d2++) {
                char probe[3] = { (char)('0' + d1), (char)('0' + d2), '\0' };
                uint64_t n = 0;
                const dealcode_err_t err =
                    dealcode_cycle_decode(dc, probe, last, &n);
                if (err == DEALCODE_OK) {
                    accepted++;
                    CHECK(n >= last * 100 && n <= top,
                          "boundary: accepted counter in range");
                } else {
                    CHECK(err == DEALCODE_ERR_INVALID_CODE,
                          "boundary: partial-cycle rejection is "
                          "invalid-code, got %s",
                          dealcode_strerror(err));
                    rejected++;
                }
            }
        }
        CHECK(rejected > 0, "boundary: final partial cycle rejects codes");
        dealcode_cycle_free(dc);
    }

    /* octal length 21: capacity exactly 2^63, a single cycle */
    {
        dealcode_cycle_config_t c = { 0 };
        c.key_string = "cycle-boundary-key";
        c.alphabet = "01234567";
        c.length = 21;
        dealcode_cycle_t *dc = NULL;
        CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK,
              "boundary: octal new");
        if (dc == NULL)
            return;
        CHECK(dealcode_cycle_capacity(dc) == (UINT64_C(1) << 63),
              "boundary: octal capacity 2^63");
        CHECK(dealcode_cycle_max_cycle(dc) == 0,
              "boundary: octal max_cycle 0");

        char code[DEALCODE_MAX_CODE_SIZE];
        uint64_t back = 0;
        CHECK(dealcode_cycle_encode(dc, top, code, sizeof code) ==
                      DEALCODE_OK &&
                  strlen(code) == 21 &&
                  dealcode_cycle_decode(dc, code, 0, &back) == DEALCODE_OK &&
                  back == top,
              "boundary: octal 2^63 - 1 roundtrip");
        CHECK(dealcode_cycle_encode(dc, 0, code, sizeof code) ==
                      DEALCODE_OK &&
                  dealcode_cycle_decode(dc, code, 0, &back) == DEALCODE_OK &&
                  back == 0,
              "boundary: octal 0 roundtrip");
        CHECK(dealcode_cycle_decode(dc, code, 1, &back) ==
                  DEALCODE_ERR_RANGE,
              "boundary: octal cycle 1 out of range");
        dealcode_cycle_free(dc);
    }
}

/* Cycling and plain codecs live in disjoint tweak namespaces: the same key,
 * alphabet, domain and fixed length must still permute differently. */
static void test_cycle_namespace_separation(void)
{
    dealcode_config_t pc = { 0 };
    pc.key_string = "namespace-key";
    pc.alphabet = "dec";
    pc.min_length = 6;
    pc.max_length = 6;
    pc.domain = "orders";
    dealcode_t *plain = NULL;
    CHECK(dealcode_new(&pc, &plain) == DEALCODE_OK, "namespace: plain new");

    dealcode_cycle_config_t cc = { 0 };
    cc.key_string = "namespace-key";
    cc.alphabet = "dec";
    cc.length = 6;
    cc.domain = "orders";
    dealcode_cycle_t *cyc = NULL;
    CHECK(dealcode_cycle_new(&cc, &cyc) == DEALCODE_OK,
          "namespace: cycle new");

    if (plain != NULL && cyc != NULL) {
        int all_equal = 1;
        for (uint64_t n = 0; n < 32; n++) {
            char a[DEALCODE_MAX_CODE_SIZE], b[DEALCODE_MAX_CODE_SIZE];
            CHECK(dealcode_encode(plain, n, a, sizeof a) == DEALCODE_OK &&
                      dealcode_cycle_encode(cyc, n, b, sizeof b) ==
                          DEALCODE_OK,
                  "namespace: encode");
            if (strcmp(a, b) != 0)
                all_equal = 0;
        }
        CHECK(!all_equal,
              "namespace: v1 and v1c tweaks must permute differently");
    }
    dealcode_free(plain);
    dealcode_cycle_free(cyc);
}

/* Longest possible cycling tweak: 255-byte domain and the largest cycle
 * number (17 digits at capacity 100) — exercises the 288-byte tweak path
 * end to end under ASan. */
static void test_cycle_max_tweak(void)
{
    static char domain[256];
    memset(domain, 'd', 255);
    domain[255] = '\0';

    dealcode_cycle_config_t c = { 0 };
    c.key_string = "max-tweak-key";
    c.alphabet = "dec";
    c.length = 2;
    c.domain = domain;
    dealcode_cycle_t *dc = NULL;
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK, "max-tweak: new");
    if (dc == NULL)
        return;

    const uint64_t top = (UINT64_C(1) << 63) - 1;
    char code[DEALCODE_MAX_CODE_SIZE];
    uint64_t back = 0;
    CHECK(dealcode_cycle_encode(dc, top, code, sizeof code) == DEALCODE_OK &&
              dealcode_cycle_decode(dc, code, top / 100, &back) ==
                  DEALCODE_OK &&
              back == top,
          "max-tweak: roundtrip at the largest cycle number");
    dealcode_cycle_free(dc);
}

static void test_cycle_null_and_buffer(void)
{
    char code[DEALCODE_MAX_CODE_SIZE];
    uint64_t n;
    CHECK(dealcode_cycle_capacity(NULL) == 0, "cycle null: capacity");
    CHECK(dealcode_cycle_max_cycle(NULL) == 0, "cycle null: max_cycle");
    CHECK(dealcode_cycle_length(NULL) == 0, "cycle null: length");
    CHECK(dealcode_cycle_radix(NULL) == 0, "cycle null: radix");
    CHECK(dealcode_cycle_alphabet(NULL) == NULL, "cycle null: alphabet");
    CHECK(dealcode_cycle_encode(NULL, 0, code, sizeof code) ==
              DEALCODE_ERR_CONFIG,
          "cycle null: encode");
    CHECK(dealcode_cycle_decode(NULL, "abc", 0, &n) == DEALCODE_ERR_CONFIG,
          "cycle null: decode");
    dealcode_cycle_free(NULL); /* must be a no-op */

    dealcode_cycle_config_t c = { 0 };
    c.key_string = "cycle-buffer-key";
    dealcode_cycle_t *dc = NULL;
    CHECK(dealcode_cycle_new(&c, &dc) == DEALCODE_OK, "cycle buffer: new");
    if (dc == NULL)
        return;
    CHECK(dealcode_cycle_decode(dc, NULL, 0, &n) ==
              DEALCODE_ERR_INVALID_CODE,
          "cycle null: decode NULL code");
    CHECK(dealcode_cycle_decode(dc, "c4334d", 0, NULL) ==
              DEALCODE_ERR_CONFIG,
          "cycle null: decode NULL n_out");

    /* buffer errors: 6-char code needs 7 bytes; nothing written on error */
    memset(code, 'Z', sizeof code);
    CHECK(dealcode_cycle_encode(dc, 0, code, 6) == DEALCODE_ERR_BUFFER,
          "cycle buffer: size 6 too small");
    CHECK(code[0] == 'Z', "cycle buffer: nothing written on ERR_BUFFER");
    CHECK(dealcode_cycle_encode(dc, 0, code, 0) == DEALCODE_ERR_BUFFER,
          "cycle buffer: size 0");
    CHECK(dealcode_cycle_encode(dc, 0, NULL, 64) == DEALCODE_ERR_BUFFER,
          "cycle buffer: NULL out");
    CHECK(dealcode_cycle_encode(dc, 0, code, 7) == DEALCODE_OK &&
              strlen(code) == 6,
          "cycle buffer: exact size succeeds");
    dealcode_cycle_free(dc);
}

/* Regression tests for the QA round-2 findings (SPEC section 2.1): string
 * key material and domains must be valid UTF-8 (invalid sequences and
 * UTF-8-encoded surrogates rejected, never silently reinterpreted). */
static void test_utf8_validation(void)
{
    dealcode_t *dc = NULL;
    dealcode_config_t c;

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.domain = "\xff\xfe";
    CHECK(dealcode_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "utf8: invalid domain bytes rejected");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.domain = "\xed\xa0\x80"; /* UTF-8-encoded surrogate U+D800 */
    CHECK(dealcode_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "utf8: encoded surrogate domain rejected");

    memset(&c, 0, sizeof c);
    c.key_string = "\xc0\xaf"; /* overlong '/' */
    CHECK(dealcode_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "utf8: overlong key string rejected");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.domain = "\xed\x9f\xbf\xee\x80\x80"; /* U+D7FF U+E000: valid */
    CHECK(dealcode_new(&c, &dc) == DEALCODE_OK, "utf8: valid 3-byte domain ok");
    dealcode_free(dc);
    dc = NULL;

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.domain = "\xf0\x9f\x98\x80"; /* U+1F600 emoji: valid */
    CHECK(dealcode_new(&c, &dc) == DEALCODE_OK, "utf8: valid 4-byte domain ok");
    dealcode_free(dc);
}

/* ---------------------------------------------------------------------- */
/* Integer range mode (SPEC section 12)                                   */
/* ---------------------------------------------------------------------- */

static dealcode_range_t *make_range_codec(const tv_range_config_t *cfg,
                                          dealcode_err_t *err_out)
{
    uint8_t key[64];
    dealcode_range_config_t c = { 0 };
    if (cfg->key_hex != NULL) {
        const size_t key_len = hex_decode(cfg->key_hex, key, sizeof key);
        if (key_len == (size_t)-1) {
            *err_out = DEALCODE_ERR_CONFIG;
            return NULL;
        }
        c.key = key;
        c.key_len = key_len;
    } else {
        c.key_string = cfg->key_string;
    }
    c.low = cfg->low;
    c.high = cfg->high;
    c.domain = cfg->domain;

    dealcode_range_t *dc = NULL;
    *err_out = dealcode_range_new(&c, &dc);
    return dc;
}

static void test_v1r_vectors(void)
{
    for (size_t i = 0; i < TV_V1R_COUNT; i++) {
        const tv_range_config_t *cfg = &TV_V1R[i];
        dealcode_err_t err;
        dealcode_range_t *dc = make_range_codec(cfg, &err);
        CHECK(dc != NULL, "v1r %s: dealcode_range_new: %s", cfg->name,
              dealcode_strerror(err));
        if (dc == NULL)
            continue;

        /* derived domain (SPEC section 12.2): radix, m, capacity must all
         * match the vector file (capacity == radix^m pins m). */
        CHECK(dealcode_range_radix(dc) == (int)cfg->radix,
              "v1r %s: radix %d, want %u", cfg->name,
              dealcode_range_radix(dc), cfg->radix);
        CHECK(dealcode_range_capacity(dc) == cfg->capacity,
              "v1r %s: capacity %" PRIu64 ", want %" PRIu64, cfg->name,
              dealcode_range_capacity(dc), cfg->capacity);
        {
            uint64_t power = 1;
            int overflow = 0;
            for (int j = 0; j < cfg->m; j++) {
                if (power > UINT64_MAX / cfg->radix) {
                    overflow = 1;
                    break;
                }
                power *= cfg->radix;
            }
            CHECK(!overflow && power == cfg->capacity,
                  "v1r %s: capacity must equal radix^m", cfg->name);
        }
        CHECK(dealcode_range_low(dc) == cfg->low &&
                  dealcode_range_high(dc) == cfg->high,
              "v1r %s: low/high accessors", cfg->name);

        for (size_t j = 0; j < cfg->n_vectors; j++) {
            const tv_range_pair_t *tv = &cfg->vectors[j];
            uint64_t code = UINT64_MAX;
            err = dealcode_range_encode(dc, tv->n, &code);
            CHECK(err == DEALCODE_OK && code == tv->code,
                  "v1r %s: encode(%" PRIu64 ") = %" PRIu64
                  " (%s), want %" PRIu64,
                  cfg->name, tv->n, code, dealcode_strerror(err), tv->code);

            uint64_t n = UINT64_MAX;
            err = dealcode_range_decode(dc, tv->code, &n);
            CHECK(err == DEALCODE_OK && n == tv->n,
                  "v1r %s: decode(%" PRIu64 ") = %" PRIu64
                  " (%s), want %" PRIu64,
                  cfg->name, tv->code, n, dealcode_strerror(err), tv->n);
        }

        for (size_t j = 0; j < cfg->n_invalid_codes; j++) {
            uint64_t n = 0;
            err = dealcode_range_decode(dc, cfg->invalid_codes[j], &n);
            CHECK(err == DEALCODE_ERR_INVALID_CODE,
                  "v1r %s: decode(%" PRIu64 ") = %s, want invalid-code",
                  cfg->name, cfg->invalid_codes[j], dealcode_strerror(err));
        }

        for (size_t j = 0; j < cfg->n_range_counters; j++) {
            uint64_t code = 0;
            err = dealcode_range_encode(dc, cfg->range_counters[j], &code);
            CHECK(err == DEALCODE_ERR_RANGE,
                  "v1r %s: encode(%" PRIu64 ") = %s, want range error",
                  cfg->name, cfg->range_counters[j], dealcode_strerror(err));
        }

        dealcode_range_free(dc);
    }
}

static void test_v1r_invalid_configs(void)
{
    for (size_t i = 0; i < TV_V1R_INVALID_CONFIG_COUNT; i++) {
        const tv_range_invalid_config_t *tv = &TV_V1R_INVALID_CONFIGS[i];
        uint8_t key[64];
        dealcode_range_config_t c = { 0 };
        if (tv->key_hex != NULL) {
            const size_t key_len = hex_decode(tv->key_hex, key, sizeof key);
            CHECK(key_len != (size_t)-1, "v1r invalid-config %s: bad key hex",
                  tv->name);
            if (key_len == (size_t)-1)
                continue;
            c.key = key;
            c.key_len = key_len;
        } else {
            c.key_string = tv->key_string;
        }
        c.low = tv->low;
        c.high = tv->high;
        c.domain = tv->domain;

        dealcode_range_t *dc = NULL;
        dealcode_err_t err = dealcode_range_new(&c, &dc);
        CHECK(err == DEALCODE_ERR_CONFIG && dc == NULL,
              "v1r invalid-config %s: got %s, want config error", tv->name,
              dealcode_strerror(err));

        /* the _ex form must agree, and must explain itself */
        char errbuf[DEALCODE_ERRBUF_SIZE];
        err = dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf);
        CHECK(err == DEALCODE_ERR_CONFIG && dc == NULL,
              "v1r invalid-config %s: _ex got %s, want config error",
              tv->name, dealcode_strerror(err));
        CHECK(errbuf[0] != '\0',
              "v1r invalid-config %s: _ex must write a diagnostic", tv->name);
        dealcode_range_free(dc);
    }
}

/* The range constructor reuses the plain codec's key and domain guards
 * verbatim, plus its own bound constraints; check the exact diagnostics. */
static void test_range_config_errors(void)
{
    dealcode_range_config_t c;
    dealcode_range_t *dc = NULL;
    char errbuf[DEALCODE_ERRBUF_SIZE];

    /* success: OK, handle set, errbuf cleared to "" */
    memset(&c, 0, sizeof c);
    c.key_string = "range-detail-key";
    c.low = 100000;
    c.high = 999999;
    memset(errbuf, 'x', sizeof errbuf);
    CHECK(dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_OK &&
              dc != NULL,
          "range new_ex: success");
    CHECK(errbuf[0] == '\0', "range new_ex: errbuf empty on success");
    dealcode_range_free(dc);
    dc = NULL;

    /* guard B: preset-name string key, same message as plain */
    memset(&c, 0, sizeof c);
    c.key_string = "crockford";
    c.low = 100000;
    c.high = 999999;
    CHECK(dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              dc == NULL,
          "range guard B: string key \"crockford\" rejected");
    CHECK(strcmp(errbuf,
                 "string key \"crockford\" is a preset alphabet name — did "
                 "you swap the key and alphabet fields?") == 0,
          "range guard B: diagnostic, got \"%s\"", errbuf);

    /* byte keys spelling a preset name are unaffected (as in plain) */
    memset(&c, 0, sizeof c);
    c.key = (const uint8_t *)"crockford";
    c.key_len = 9;
    c.low = 100000;
    c.high = 999999;
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_OK,
          "range guard B: byte key \"crockford\" accepted");
    dealcode_range_free(dc);
    dc = NULL;

    /* empty key, as in plain */
    memset(&c, 0, sizeof c);
    c.key_string = "";
    c.low = 100000;
    c.high = 999999;
    CHECK(dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "key: empty") == 0,
          "range: empty key message, got \"%s\"", errbuf);

    /* bound constraints (SPEC section 12.1) */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.low = 0;
    c.high = UINT64_C(1) << 63; /* one past 2^63 - 1 */
    CHECK(dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf,
                     "high 9223372036854775808 exceeds 2^63 - 1") == 0,
          "range: high bound message, got \"%s\"", errbuf);

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.low = 0;
    c.high = UINT64_MAX;
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "range: high UINT64_MAX rejected");

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.low = 10;
    c.high = 9;
    CHECK(dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf, "low 10 > high 9") == 0,
          "range: low > high message, got \"%s\"", errbuf);

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.low = 0;
    c.high = 98; /* span 99 < 100 */
    CHECK(dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                  DEALCODE_ERR_CONFIG &&
              strcmp(errbuf,
                     "range [0, 98] spans 99 values; FF1 needs at "
                     "least 100") == 0,
          "range: span message, got \"%s\"", errbuf);

    /* oversized and invalid-UTF-8 domains: same rules as plain */
    {
        static char domain[300];
        memset(domain, 'a', 256);
        domain[256] = '\0';
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        c.low = 100000;
        c.high = 999999;
        c.domain = domain;
        CHECK(dealcode_range_new_ex(&c, &dc, errbuf, sizeof errbuf) ==
                      DEALCODE_ERR_CONFIG &&
                  strcmp(errbuf,
                         "domain exceeds 255 UTF-8 bytes (got 256)") == 0,
              "range: domain message, got \"%s\"", errbuf);
    }
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.low = 100000;
    c.high = 999999;
    c.domain = "\xff\xfe";
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "range: invalid UTF-8 domain rejected");
    memset(&c, 0, sizeof c);
    c.key_string = "\xc0\xaf";
    c.low = 100000;
    c.high = 999999;
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_ERR_CONFIG,
          "range: invalid UTF-8 key string rejected");

    /* boundaries that must SUCCEED */
    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.low = 0;
    c.high = 99; /* span exactly 100 */
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_OK,
          "range: span == 100 must be accepted");
    if (dc != NULL)
        CHECK(dealcode_range_capacity(dc) == 100 &&
                  dealcode_range_radix(dc) == 10,
              "range: span 100 -> 10^2");
    dealcode_range_free(dc);
    dc = NULL;

    memset(&c, 0, sizeof c);
    c.key_string = "k";
    c.low = 0;
    c.high = (UINT64_C(1) << 63) - 1; /* full counter space, N = 2^63 */
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_OK,
          "range: high == 2^63 - 1 must be accepted");
    if (dc != NULL)
        CHECK(dealcode_range_capacity(dc) == (UINT64_C(1) << 63) &&
                  dealcode_range_radix(dc) == 128,
              "range: N == 2^63 -> 128^9, capacity 2^63");
    dealcode_range_free(dc);
    dc = NULL;

    /* NULL argument handling */
    {
        dealcode_range_t *out = NULL;
        CHECK(dealcode_range_new(NULL, &out) == DEALCODE_ERR_CONFIG,
              "range: NULL cfg");
        memset(&c, 0, sizeof c);
        c.key_string = "k";
        c.low = 100000;
        c.high = 999999;
        CHECK(dealcode_range_new(&c, NULL) == DEALCODE_ERR_CONFIG,
              "range: NULL out");
    }
}

/* SPEC section 12.3/12.4 behaviour: with N itself an admissible power the
 * whole range is covered — encode is a bijection [0, N) <-> [low, high]. */
static void test_range_bijection(void)
{
    dealcode_range_config_t c = { 0 };
    c.key_string = "range-behaviour-key";
    c.low = 1000;
    c.high = 1120; /* N = 121 = 11^2: no dead zone */
    dealcode_range_t *dc = NULL;
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_OK, "bijection: new");
    if (dc == NULL)
        return;
    CHECK(dealcode_range_capacity(dc) == 121 &&
              dealcode_range_radix(dc) == 11,
          "bijection: capacity 121 = 11^2");

    int seen[121] = { 0 };
    for (uint64_t n = 0; n < 121; n++) {
        uint64_t code = 0;
        CHECK(dealcode_range_encode(dc, n, &code) == DEALCODE_OK,
              "bijection: encode(%" PRIu64 ")", n);
        CHECK(code >= 1000 && code <= 1120,
              "bijection: code %" PRIu64 " in [1000, 1120]", code);
        const int slot = (int)(code - 1000);
        CHECK(!seen[slot], "bijection: codes must be distinct");
        seen[slot] = 1;

        uint64_t back = UINT64_MAX;
        CHECK(dealcode_range_decode(dc, code, &back) == DEALCODE_OK &&
                  back == n,
              "bijection: decode(%" PRIu64 ")", code);
    }
    int covered = 1;
    for (int s = 0; s < 121; s++)
        covered = covered && seen[s];
    CHECK(covered, "bijection: the 121 codes cover the whole range");
    dealcode_range_free(dc);
}

/* The dead zone [low + capacity, high] is never issued and is rejected by
 * decode; the issued top (low + capacity - 1) still roundtrips. */
static void test_range_dead_zone(void)
{
    dealcode_range_config_t c = { 0 };
    c.key_string = "range-behaviour-key";
    c.low = 100000;
    c.high = 999999; /* N = 900000 -> 96^3 = 884736 */
    dealcode_range_t *dc = NULL;
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_OK, "dead-zone: new");
    if (dc == NULL)
        return;
    const uint64_t capacity = dealcode_range_capacity(dc);
    CHECK(capacity == 884736, "dead-zone: capacity 96^3");
    const uint64_t top_issued = 100000 + capacity - 1; /* 984735 */

    uint64_t code = 0, back = UINT64_MAX;
    CHECK(dealcode_range_encode(dc, capacity - 1, &code) == DEALCODE_OK &&
              code >= 100000 && code <= top_issued &&
              dealcode_range_decode(dc, code, &back) == DEALCODE_OK &&
              back == capacity - 1,
          "dead-zone: last counter roundtrips inside the issued slice");

    static const uint64_t dead[] = { UINT64_C(984736), UINT64_C(990000),
                                     UINT64_C(999999) };
    for (size_t i = 0; i < sizeof dead / sizeof dead[0]; i++) {
        uint64_t n = 0;
        CHECK(dealcode_range_decode(dc, dead[i], &n) ==
                  DEALCODE_ERR_INVALID_CODE,
              "dead-zone: decode(%" PRIu64 ") must be invalid", dead[i]);
    }
    /* below low / above high are invalid too */
    uint64_t n = 0;
    CHECK(dealcode_range_decode(dc, 99999, &n) == DEALCODE_ERR_INVALID_CODE,
          "dead-zone: below low rejected");
    CHECK(dealcode_range_decode(dc, 1000000, &n) ==
              DEALCODE_ERR_INVALID_CODE,
          "dead-zone: above high rejected");
    /* range errors on encode */
    CHECK(dealcode_range_encode(dc, capacity, &code) == DEALCODE_ERR_RANGE,
          "dead-zone: encode(capacity) rejected");
    CHECK(dealcode_range_encode(dc, UINT64_MAX, &code) ==
              DEALCODE_ERR_RANGE,
          "dead-zone: encode(UINT64_MAX) rejected");
    dealcode_range_free(dc);
}

/* low, high and domain are all bound into the tweak (SPEC section 12.3):
 * changing any one of them must change the permutation. */
static void test_range_binding(void)
{
    dealcode_range_config_t c = { 0 };
    c.key_string = "range-binding-key";
    c.low = 100000;
    c.high = 999999;
    dealcode_range_t *a = NULL, *b = NULL, *d = NULL;
    CHECK(dealcode_range_new(&c, &a) == DEALCODE_OK, "binding: new a");
    c.high = 999998; /* same derived domain (96^3), different tweak */
    CHECK(dealcode_range_new(&c, &b) == DEALCODE_OK, "binding: new b");
    c.high = 999999;
    c.domain = "x";
    CHECK(dealcode_range_new(&c, &d) == DEALCODE_OK, "binding: new d");

    if (a != NULL && b != NULL && d != NULL) {
        CHECK(dealcode_range_capacity(b) == dealcode_range_capacity(a),
              "binding: same capacity for the comparison to be fair");
        int ab_equal = 1, ad_equal = 1, bd_equal = 1;
        for (uint64_t n = 0; n < 32; n++) {
            uint64_t ca = 0, cb = 0, cd = 0;
            CHECK(dealcode_range_encode(a, n, &ca) == DEALCODE_OK &&
                      dealcode_range_encode(b, n, &cb) == DEALCODE_OK &&
                      dealcode_range_encode(d, n, &cd) == DEALCODE_OK,
                  "binding: encode");
            ab_equal = ab_equal && ca == cb;
            ad_equal = ad_equal && ca == cd;
            bd_equal = bd_equal && cb == cd;
        }
        CHECK(!ab_equal && !ad_equal && !bd_equal,
              "binding: low/high/domain must all bind the permutation");
    }
    dealcode_range_free(a);
    dealcode_range_free(b);
    dealcode_range_free(d);
}

static void test_range_null_handles(void)
{
    uint64_t code = 0, n = 0;
    CHECK(dealcode_range_capacity(NULL) == 0, "range null: capacity");
    CHECK(dealcode_range_low(NULL) == 0, "range null: low");
    CHECK(dealcode_range_high(NULL) == 0, "range null: high");
    CHECK(dealcode_range_radix(NULL) == 0, "range null: radix");
    CHECK(dealcode_range_encode(NULL, 0, &code) == DEALCODE_ERR_CONFIG,
          "range null: encode");
    CHECK(dealcode_range_decode(NULL, 100000, &n) == DEALCODE_ERR_CONFIG,
          "range null: decode");
    dealcode_range_free(NULL); /* must be a no-op */

    dealcode_range_config_t c = { 0 };
    c.key_string = "k";
    c.low = 100000;
    c.high = 999999;
    dealcode_range_t *dc = NULL;
    CHECK(dealcode_range_new(&c, &dc) == DEALCODE_OK, "range null: new");
    if (dc != NULL) {
        CHECK(dealcode_range_encode(dc, 0, NULL) == DEALCODE_ERR_CONFIG,
              "range null: encode NULL code_out");
        CHECK(dealcode_range_decode(dc, 100000, NULL) ==
                  DEALCODE_ERR_CONFIG,
              "range null: decode NULL n_out");
    }
    dealcode_range_free(dc);
}

/* ---------------------------------------------------------------------- */

int main(void)
{
    test_nist_ff1();
    test_v1_vectors();
    test_v1_invalid_configs();
    test_buffer_errors();
    test_range_errors();
    test_config_errors();
    test_preset_name_alphabet_guard();
    test_preset_name_key_guard();
    test_new_ex_details();
    test_defaults();
    test_roundtrips();
    test_counter_bound_rejection();
    test_domain_separation();
    test_strerror();
    test_null_handles();
    test_utf8_validation();
    test_v1c_vectors();
    test_v1c_invalid_configs();
    test_cycle_config_errors();
    test_cycle_permutation_behaviour();
    test_cycle_boundaries();
    test_cycle_namespace_separation();
    test_cycle_max_tweak();
    test_cycle_null_and_buffer();
    test_v1r_vectors();
    test_v1r_invalid_configs();
    test_range_config_errors();
    test_range_bijection();
    test_range_dead_zone();
    test_range_binding();
    test_range_null_handles();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures != 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
