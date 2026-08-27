/*
 * Test suite for the dealcode C library.
 *
 * Covers:
 *  - the 9 official NIST FF1 sample vectors (via the private FF1 seam)
 *  - every config/vector/invalid-code/normalize case in testvectors/v1.json
 *  - buffer, range, and config error behaviour
 *  - large roundtrip loops across stage boundaries, including configs where
 *    radix^max_length is exactly 2^128
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

int main(void)
{
    test_nist_ff1();
    test_v1_vectors();
    test_buffer_errors();
    test_range_errors();
    test_config_errors();
    test_defaults();
    test_roundtrips();
    test_counter_bound_rejection();
    test_domain_separation();
    test_strerror();
    test_null_handles();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures != 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
