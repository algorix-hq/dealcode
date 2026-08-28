/*
 * dealcode.c — dealcode format version 1 (see SPEC.md) and its FF1 core
 * (NIST SP 800-38G, Algorithms 7 and 8).
 *
 * External dependency: OpenSSL libcrypto (EVP AES-ECB for block encryption,
 * SHA-256 for key derivation). Link with -lcrypto.
 *
 * Wide arithmetic uses unsigned __int128 (GCC/Clang). Key invariants that
 * make 128-bit arithmetic sufficient (SPEC.md §6, §7):
 *   - radix^max_length <= 2^128, so radix^d for d <= max_length - 1 is
 *     <= 2^127 and always representable; radix^max_length itself may be
 *     exactly 2^128 (one past the u128 range) and is therefore never
 *     materialized: powers are tracked as radix^d - 1 during validation, the
 *     decode stage check uses radix^(d-1) * (radix-1) = radix^d - radix^(d-1)
 *     (always < 2^128), and the counter bound check uses base + v >= 2^63.
 *   - within these bounds radix^v < 2^68 for FF1's right-half length v, so
 *     b <= 9, d_len <= 16, and all modular folds fit comfortably in u128.
 */

#include "dealcode.h"
#include "ff1.h"

#if !defined(__SIZEOF_INT128__)
#error "dealcode requires unsigned __int128 (GCC or Clang)"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

typedef unsigned __int128 u128;

#define TWO63 ((u128)1 << 63)

/* ====================================================================== */
/* FF1 core (NIST SP 800-38G)                                             */
/* ====================================================================== */

/* FF1 works for any parameters with radix^v < 2^96; dealcode itself only
 * ever needs radix^v < 2^68. The 2^96 headroom keeps every intermediate
 * (byte folds: acc*256 + byte < 2^104; numeral folds: acc*radix + digit
 * < 2^104) safely inside u128. */
#define FF1_MAX_HALF_BITS 96
#define FF1_MAX_N 128

dealcode_err_t dealcode_ff1_init(dealcode_ff1_t *ff1, const uint8_t *key,
                                 size_t key_len, unsigned radix)
{
    if (ff1 == NULL || key == NULL)
        return DEALCODE_ERR_CONFIG;
    if (key_len != 16 && key_len != 24 && key_len != 32)
        return DEALCODE_ERR_CONFIG;
    if (radix < 2 || radix > 256)
        return DEALCODE_ERR_CONFIG;
    memset(ff1, 0, sizeof *ff1);
    memcpy(ff1->key, key, key_len);
    ff1->key_len = key_len;
    ff1->radix = radix;
    return DEALCODE_OK;
}

static int u128_bitlen(u128 x)
{
    int n = 0;
    while (x != 0) {
        x >>= 1;
        n++;
    }
    return n;
}

/* radix^exp with overflow rejection above 2^FF1_MAX_HALF_BITS. */
static dealcode_err_t ff1_pow(unsigned radix, size_t exp, u128 *out)
{
    u128 acc = 1;
    const u128 limit = (u128)1 << FF1_MAX_HALF_BITS;
    for (size_t i = 0; i < exp; i++) {
        acc *= radix; /* acc < 2^96 before, radix <= 256: no u128 overflow */
        if (acc >= limit)
            return DEALCODE_ERR_CONFIG;
    }
    *out = acc;
    return DEALCODE_OK;
}

/* NUM_radix(X): big-endian numeral string to integer. */
static u128 ff1_num(const uint8_t *xs, size_t len, unsigned radix)
{
    u128 acc = 0;
    for (size_t i = 0; i < len; i++)
        acc = acc * radix + xs[i];
    return acc;
}

/* STR_radix^m(val): integer to m big-endian numerals. */
static void ff1_str(u128 val, unsigned radix, size_t m, uint8_t *out)
{
    for (size_t i = m; i-- > 0;) {
        out[i] = (uint8_t)(val % radix);
        val /= radix;
    }
}

/* One AES-ECB block through an already-initialized encrypt context. */
static dealcode_err_t ff1_ciph(EVP_CIPHER_CTX *ctx, const uint8_t in[16],
                               uint8_t out[16])
{
    int outl = 0;
    if (EVP_EncryptUpdate(ctx, out, &outl, in, 16) != 1 || outl != 16)
        return DEALCODE_ERR_CRYPTO;
    return DEALCODE_OK;
}

/* PRF(X): AES-CBC-MAC with a zero IV over a 16-byte-multiple input. */
static dealcode_err_t ff1_prf(EVP_CIPHER_CTX *ctx, const uint8_t *data,
                              size_t len, uint8_t mac[16])
{
    uint8_t x[16];
    memset(mac, 0, 16);
    for (size_t off = 0; off < len; off += 16) {
        for (size_t k = 0; k < 16; k++)
            x[k] = (uint8_t)(mac[k] ^ data[off + k]);
        dealcode_err_t err = ff1_ciph(ctx, x, mac);
        if (err != DEALCODE_OK)
            return err;
    }
    return DEALCODE_OK;
}

/* One Feistel round's y value, reduced mod `mod`:
 *   Q tail <- [i]^1 || [num]^b ; R <- PRF(Q) ;
 *   S <- R || CIPH(R xor [1]^16) || CIPH(R xor [2]^16) || ... (d_len bytes) ;
 *   y <- NUM(S) mod mod  (computed by byte-folding, never materializing
 *   NUM(S) itself, so d_len may exceed 16 without overflow). */
static dealcode_err_t ff1_round_y(EVP_CIPHER_CTX *ctx, uint8_t *q, size_t qlen,
                                  size_t num_off, size_t b, size_t d_len,
                                  int round, u128 num, u128 mod, u128 *y_out)
{
    q[num_off] = (uint8_t)round;
    for (size_t j = b; j-- > 0;) {
        q[num_off + 1 + j] = (uint8_t)(num & 0xff);
        num >>= 8;
    }

    uint8_t r[16], blk[16], xin[16];
    dealcode_err_t err = ff1_prf(ctx, q, qlen, r);
    if (err != DEALCODE_OK)
        return err;

    u128 acc = 0;
    size_t taken = 0;
    const uint8_t *src = r;
    uint32_t block_index = 1;
    while (taken < d_len) {
        size_t chunk = d_len - taken;
        if (chunk > 16)
            chunk = 16;
        for (size_t k = 0; k < chunk; k++)
            acc = (acc * 256 + src[k]) % mod;
        taken += chunk;
        if (taken < d_len) {
            /* next block: CIPH(R xor [block_index]^16), index big-endian */
            memcpy(xin, r, 16);
            xin[12] ^= (uint8_t)(block_index >> 24);
            xin[13] ^= (uint8_t)(block_index >> 16);
            xin[14] ^= (uint8_t)(block_index >> 8);
            xin[15] ^= (uint8_t)block_index;
            err = ff1_ciph(ctx, xin, blk);
            if (err != DEALCODE_OK)
                return err;
            src = blk;
            block_index++;
        }
    }
    *y_out = acc;
    return DEALCODE_OK;
}

/* Shared body of FF1.Encrypt / FF1.Decrypt. */
static dealcode_err_t ff1_run(const dealcode_ff1_t *ff1, const uint8_t *tweak,
                              size_t tweak_len, const uint8_t *x, size_t n,
                              uint8_t *out, int decrypt)
{
    if (ff1 == NULL || x == NULL || out == NULL)
        return DEALCODE_ERR_CONFIG;
    if (tweak == NULL && tweak_len != 0)
        return DEALCODE_ERR_CONFIG;
    const unsigned radix = ff1->radix;
    if (n < 2 || n > FF1_MAX_N)
        return DEALCODE_ERR_CONFIG;
    for (size_t i = 0; i < n; i++)
        if (x[i] >= radix)
            return DEALCODE_ERR_CONFIG;

    const size_t u = n / 2;
    const size_t v = n - u;
    u128 mod_u, mod_v;
    dealcode_err_t err = ff1_pow(radix, v, &mod_v);
    if (err != DEALCODE_OK)
        return err;
    err = ff1_pow(radix, u, &mod_u);
    if (err != DEALCODE_OK)
        return err;
    /* radix^n >= 100 (FF1 minimum domain); radix^n = mod_u * mod_v but that
     * product can overflow, so check against 100 via the halves. */
    if (mod_u < 100 && mod_v < 100 && mod_u * mod_v < 100)
        return DEALCODE_ERR_CONFIG;

    /* b = ceil(ceil(v * log2(radix)) / 8) computed exactly as the byte
     * length of radix^v - 1 (never floating-point). */
    const size_t b = (size_t)(u128_bitlen(mod_v - 1) + 7) / 8;
    const size_t d_len = 4 * ((b + 3) / 4) + 4;

    /* Q = P || tweak || [0]^pad || [round]^1 || [NUM]^b, |Q| multiple of 16 */
    const size_t pad = (16 - ((tweak_len + b + 1) % 16)) % 16;
    const size_t qlen = 16 + tweak_len + pad + 1 + b;
    const size_t num_off = 16 + tweak_len + pad;

    uint8_t *q = calloc(1, qlen);
    if (q == NULL)
        return DEALCODE_ERR_NOMEM;
    /* P (16 bytes) per Algorithm 7 step 5. */
    q[0] = 1;
    q[1] = 2;
    q[2] = 1;
    q[3] = (uint8_t)(radix >> 16);
    q[4] = (uint8_t)(radix >> 8);
    q[5] = (uint8_t)radix;
    q[6] = 10;
    q[7] = (uint8_t)(u & 0xff);
    q[8] = (uint8_t)(n >> 24);
    q[9] = (uint8_t)(n >> 16);
    q[10] = (uint8_t)(n >> 8);
    q[11] = (uint8_t)n;
    q[12] = (uint8_t)(tweak_len >> 24);
    q[13] = (uint8_t)(tweak_len >> 16);
    q[14] = (uint8_t)(tweak_len >> 8);
    q[15] = (uint8_t)tweak_len;
    if (tweak_len > 0)
        memcpy(q + 16, tweak, tweak_len);
    /* pad bytes already zero (calloc); round byte + NUM filled per round */

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        free(q);
        return DEALCODE_ERR_NOMEM;
    }
    const EVP_CIPHER *cipher = ff1->key_len == 16   ? EVP_aes_128_ecb()
                               : ff1->key_len == 24 ? EVP_aes_192_ecb()
                                                    : EVP_aes_256_ecb();
    if (EVP_EncryptInit_ex(ctx, cipher, NULL, ff1->key, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        free(q);
        return DEALCODE_ERR_CRYPTO;
    }
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    uint8_t a_buf[FF1_MAX_N], b_buf[FF1_MAX_N];
    size_t la = u, lb = v;
    memcpy(a_buf, x, u);
    memcpy(b_buf, x + u, v);

    err = DEALCODE_OK;
    for (int round = 0; round < 10; round++) {
        const int i = decrypt ? 9 - round : round;
        const size_t m = (i % 2 == 0) ? u : v;
        const u128 mod = (i % 2 == 0) ? mod_u : mod_v;
        u128 y;
        if (!decrypt) {
            err = ff1_round_y(ctx, q, qlen, num_off, b, d_len, i,
                              ff1_num(b_buf, lb, radix), mod, &y);
            if (err != DEALCODE_OK)
                break;
            const u128 c = (ff1_num(a_buf, la, radix) + y) % mod;
            memcpy(a_buf, b_buf, lb); /* A <- B */
            la = lb;
            ff1_str(c, radix, m, b_buf); /* B <- C */
            lb = m;
        } else {
            err = ff1_round_y(ctx, q, qlen, num_off, b, d_len, i,
                              ff1_num(a_buf, la, radix), mod, &y);
            if (err != DEALCODE_OK)
                break;
            const u128 c = (ff1_num(b_buf, lb, radix) + mod - y) % mod;
            memcpy(b_buf, a_buf, la); /* B <- A */
            lb = la;
            ff1_str(c, radix, m, a_buf); /* A <- C */
            la = m;
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    free(q);
    if (err != DEALCODE_OK)
        return err;

    memcpy(out, a_buf, la);
    memcpy(out + la, b_buf, lb);
    return DEALCODE_OK;
}

dealcode_err_t dealcode_ff1_encrypt(const dealcode_ff1_t *ff1,
                                    const uint8_t *tweak, size_t tweak_len,
                                    const uint8_t *x, size_t n, uint8_t *out)
{
    return ff1_run(ff1, tweak, tweak_len, x, n, out, 0);
}

dealcode_err_t dealcode_ff1_decrypt(const dealcode_ff1_t *ff1,
                                    const uint8_t *tweak, size_t tweak_len,
                                    const uint8_t *x, size_t n, uint8_t *out)
{
    return ff1_run(ff1, tweak, tweak_len, x, n, out, 1);
}

/* ====================================================================== */
/* Alphabets (SPEC.md §3)                                                 */
/* ====================================================================== */

typedef enum {
    NORM_NONE,
    NORM_LOWER,     /* ASCII-lowercase A-Z only */
    NORM_UPPER,     /* ASCII-uppercase a-z only */
    NORM_CROCKFORD  /* ASCII-uppercase, then O->0, I->1, L->1 */
} dc_norm_t;

typedef struct {
    const char *name;
    const char *chars;
    dc_norm_t norm;
} dc_preset_t;

static const dc_preset_t DC_PRESETS[] = {
    { "dec", "0123456789", NORM_NONE },
    { "hex", "0123456789abcdef", NORM_LOWER },
    { "base32", "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", NORM_UPPER },
    { "crockford", "0123456789ABCDEFGHJKMNPQRSTVWXYZ", NORM_CROCKFORD },
    { "base36", "0123456789abcdefghijklmnopqrstuvwxyz", NORM_LOWER },
    { "base58", "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz",
      NORM_NONE },
    { "base62",
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
      NORM_NONE },
    { "base64url",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
      NORM_NONE },
};

/* Case-insensitive ASCII equality against an all-lowercase reference. */
static int dc_ascii_ieq(const char *s, const char *lower_ref)
{
    size_t i = 0;
    for (; s[i] != '\0' && lower_ref[i] != '\0'; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != lower_ref[i])
            return 0;
    }
    return s[i] == '\0' && lower_ref[i] == '\0';
}

/* The preset name that `s` equals ASCII-case-insensitively, or NULL. */
static const char *dc_preset_name_ci(const char *s)
{
    for (size_t i = 0; i < sizeof DC_PRESETS / sizeof DC_PRESETS[0]; i++)
        if (dc_ascii_ieq(s, DC_PRESETS[i].name))
            return DC_PRESETS[i].name;
    return NULL;
}

static char dc_norm_char(char c, dc_norm_t norm)
{
    switch (norm) {
    case NORM_LOWER:
        return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    case NORM_UPPER:
        return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    case NORM_CROCKFORD: {
        char up = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        if (up == 'O')
            return '0';
        if (up == 'I' || up == 'L')
            return '1';
        return up;
    }
    case NORM_NONE:
    default:
        return c;
    }
}

/* ====================================================================== */
/* Codec                                                                  */
/* ====================================================================== */

#define DC_TWEAK_PREFIX "dealcode/v1/"
#define DC_TWEAK_PREFIX_LEN 12
#define DC_KDF_PREFIX "dealcode/v1/kdf"
#define DC_KDF_PREFIX_LEN 15
#define DC_MAX_DOMAIN 255
#define DC_MAX_LENGTH 128 /* radix >= 2 and radix^M <= 2^128 => M <= 128 */

struct dealcode_st {
    dealcode_ff1_t ff1;
    char alphabet[95]; /* up to 94 chars + NUL */
    unsigned radix;
    int min_length;
    int max_length;
    dc_norm_t norm;
    int16_t index[256]; /* char -> numeral, or -1 */
    uint8_t tweak[DC_TWEAK_PREFIX_LEN + DC_MAX_DOMAIN];
    size_t tweak_len;
    uint64_t capacity; /* min(radix^max_length, 2^63) */
    /* powers[d] = radix^d for 0 <= d <= max_length - 1. These are always
     * representable: radix^(max_length-1) <= 2^128 / radix <= 2^127.
     * radix^max_length itself (possibly exactly 2^128) is never stored. */
    u128 powers[DC_MAX_LENGTH];
};

/* Formats a one-line diagnostic into errbuf (NULL/0 tolerated; truncates). */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void dc_errf(char *errbuf, size_t errbuf_len, const char *fmt, ...)
{
    if (errbuf == NULL || errbuf_len == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, errbuf_len, fmt, ap);
    va_end(ap);
}

/* Validates UTF-8 (RFC 3629: no overlongs, no surrogates, <= U+10FFFF).
 * SPEC.md §2.1 requires string key material and domains to be valid Unicode
 * so every language derives the same bytes from the "same" input. Embedded
 * U+0000 is unrepresentable through this NUL-terminated API by construction. */
static int dc_valid_utf8(const uint8_t *s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint8_t b = s[i];
        if (b < 0x80) {
            i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            if (b < 0xC2 || i + 1 >= len || (s[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 ||
                (s[i + 2] & 0xC0) != 0x80)
                return 0;
            if (b == 0xE0 && s[i + 1] < 0xA0)
                return 0; /* overlong */
            if (b == 0xED && s[i + 1] >= 0xA0)
                return 0; /* surrogate */
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            if (b > 0xF4 || i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 ||
                (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80)
                return 0;
            if (b == 0xF0 && s[i + 1] < 0x90)
                return 0; /* overlong */
            if (b == 0xF4 && s[i + 1] >= 0x90)
                return 0; /* > U+10FFFF */
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

/* Key material rules (SPEC.md §2.1). Writes 16/24/32 bytes into aes_key.
 * Diagnostics never echo key material, except the preset-name guard: a key
 * rejected for *being* a preset alphabet name is not a secret. */
static dealcode_err_t dc_resolve_key(const dealcode_config_t *cfg,
                                     uint8_t aes_key[32], size_t *aes_key_len,
                                     char *errbuf, size_t errbuf_len)
{
    const uint8_t *material;
    size_t material_len;
    int is_string;

    if (cfg->key != NULL && cfg->key_string != NULL) {
        dc_errf(errbuf, errbuf_len,
                "key: both key bytes and key_string set — set exactly one");
        return DEALCODE_ERR_CONFIG;
    }
    if (cfg->key != NULL) {
        material = cfg->key;
        material_len = cfg->key_len;
        is_string = 0;
    } else if (cfg->key_string != NULL) {
        material = (const uint8_t *)cfg->key_string;
        material_len = strlen(cfg->key_string);
        if (!dc_valid_utf8(material, material_len)) {
            dc_errf(errbuf, errbuf_len,
                    "key: key_string is not valid UTF-8");
            return DEALCODE_ERR_CONFIG;
        }
        is_string = 1;
    } else {
        dc_errf(errbuf, errbuf_len,
                "key: no key material (set key/key_len or key_string)");
        return DEALCODE_ERR_CONFIG;
    }
    if (material_len == 0) {
        dc_errf(errbuf, errbuf_len, "key: empty");
        return DEALCODE_ERR_CONFIG;
    }

    if (is_string) {
        /* Swapped-arguments guard (SPEC.md §2.1): a string key that is a
         * preset alphabet name (ASCII-case-insensitively) is rejected. */
        const char *preset = dc_preset_name_ci(cfg->key_string);
        if (preset != NULL) {
            dc_errf(errbuf, errbuf_len,
                    "string key \"%s\" is a preset alphabet name — did you "
                    "swap the key and alphabet fields?",
                    cfg->key_string);
            return DEALCODE_ERR_CONFIG;
        }
    }

    if (!is_string &&
        (material_len == 16 || material_len == 24 || material_len == 32)) {
        memcpy(aes_key, material, material_len);
        *aes_key_len = material_len;
        return DEALCODE_OK;
    }

    /* AES-256 key = SHA-256("dealcode/v1/kdf" || material) */
    EVP_MD_CTX *md = EVP_MD_CTX_new();
    if (md == NULL) {
        dc_errf(errbuf, errbuf_len, "out of memory");
        return DEALCODE_ERR_NOMEM;
    }
    unsigned int digest_len = 0;
    int ok = EVP_DigestInit_ex(md, EVP_sha256(), NULL) == 1 &&
             EVP_DigestUpdate(md, DC_KDF_PREFIX, DC_KDF_PREFIX_LEN) == 1 &&
             EVP_DigestUpdate(md, material, material_len) == 1 &&
             EVP_DigestFinal_ex(md, aes_key, &digest_len) == 1;
    EVP_MD_CTX_free(md);
    if (!ok || digest_len != 32) {
        dc_errf(errbuf, errbuf_len, "OpenSSL failure during key derivation");
        return DEALCODE_ERR_CRYPTO;
    }
    *aes_key_len = 32;
    return DEALCODE_OK;
}

/* Resolve preset name or custom alphabet into chars + normalization. */
static dealcode_err_t dc_resolve_alphabet(const char *alphabet,
                                          const char **chars_out,
                                          dc_norm_t *norm_out,
                                          char *errbuf, size_t errbuf_len)
{
    if (alphabet == NULL)
        alphabet = "hex";
    for (size_t i = 0; i < sizeof DC_PRESETS / sizeof DC_PRESETS[0]; i++) {
        if (strcmp(alphabet, DC_PRESETS[i].name) == 0) {
            *chars_out = DC_PRESETS[i].chars;
            *norm_out = DC_PRESETS[i].norm;
            return DEALCODE_OK;
        }
    }
    /* Preset-name-in-disguise guard (SPEC.md §3.2): not exactly a preset
     * name, but ASCII-case-insensitively equal to one ("HEX", "Base62").
     * Accepting it would silently build a codec over the letters of the
     * name itself. */
    {
        const char *preset = dc_preset_name_ci(alphabet);
        if (preset != NULL) {
            dc_errf(errbuf, errbuf_len,
                    "custom alphabet \"%s\" matches the preset name \"%s\" — "
                    "pass \"%s\" for the preset, or a genuinely custom "
                    "alphabet",
                    alphabet, preset, preset);
            return DEALCODE_ERR_CONFIG;
        }
    }
    /* custom: 2-94 distinct printable ASCII chars (0x21-0x7E) */
    size_t len = strlen(alphabet);
    if (len < 2 || len > 94) {
        dc_errf(errbuf, errbuf_len,
                "alphabet: custom alphabet must have 2-94 characters "
                "(got %zu)",
                len);
        return DEALCODE_ERR_CONFIG;
    }
    int seen[256] = { 0 };
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)alphabet[i];
        if (c < 0x21 || c > 0x7e) {
            dc_errf(errbuf, errbuf_len,
                    "alphabet: byte 0x%02x at index %zu is not printable "
                    "ASCII (0x21-0x7E)",
                    c, i);
            return DEALCODE_ERR_CONFIG;
        }
        if (seen[c]) {
            dc_errf(errbuf, errbuf_len,
                    "alphabet: duplicate character '%c'", (char)c);
            return DEALCODE_ERR_CONFIG;
        }
        seen[c] = 1;
    }
    *chars_out = alphabet;
    *norm_out = NORM_NONE;
    return DEALCODE_OK;
}

/* Default max_length: the largest L with radix^L <= 2^63 - 1, but never
 * below min_length. q tracks radix^L - 1 (always representable). */
static int dc_default_max_length(unsigned radix, int min_length, u128 q_min)
{
    int length = min_length;
    u128 q = q_min;
    for (;;) {
        if (q >= TWO63 - 1)
            break; /* radix^length already >= 2^63; also prevents overflow */
        u128 q_next = q * radix + (radix - 1); /* radix^(length+1) - 1 */
        if (q_next < TWO63 - 1) { /* radix^(length+1) < 2^63 */
            q = q_next;
            length++;
        } else {
            break;
        }
    }
    return length;
}

dealcode_err_t dealcode_new_ex(const dealcode_config_t *cfg, dealcode_t **out,
                               char *errbuf, size_t errbuf_len)
{
    if (errbuf != NULL && errbuf_len > 0)
        errbuf[0] = '\0';
    if (out == NULL) {
        dc_errf(errbuf, errbuf_len, "out: NULL");
        return DEALCODE_ERR_CONFIG;
    }
    *out = NULL;
    if (cfg == NULL) {
        dc_errf(errbuf, errbuf_len, "cfg: NULL");
        return DEALCODE_ERR_CONFIG;
    }

    uint8_t aes_key[32];
    size_t aes_key_len = 0;
    dealcode_err_t err =
        dc_resolve_key(cfg, aes_key, &aes_key_len, errbuf, errbuf_len);
    if (err != DEALCODE_OK)
        return err;

    const char *chars = NULL;
    dc_norm_t norm = NORM_NONE;
    err = dc_resolve_alphabet(cfg->alphabet, &chars, &norm, errbuf,
                              errbuf_len);
    if (err != DEALCODE_OK)
        goto wipe_key;
    const unsigned radix = (unsigned)strlen(chars);

    int min_length = cfg->min_length == 0 ? 6 : cfg->min_length;
    if (min_length < 2) {
        dc_errf(errbuf, errbuf_len, "min_length %d < 2", cfg->min_length);
        err = DEALCODE_ERR_CONFIG;
        goto wipe_key;
    }
    if (min_length > DC_MAX_LENGTH) {
        dc_errf(errbuf, errbuf_len, "min_length %d > %d", min_length,
                DC_MAX_LENGTH);
        err = DEALCODE_ERR_CONFIG;
        goto wipe_key;
    }
    /* q = radix^d - 1, grown step by step with overflow detection;
     * radix^d <= 2^128 iff q is representable. */
    const u128 q_limit = (~(u128)0 - (radix - 1)) / radix;
    u128 q = 0;
    for (int d = 1; d <= min_length; d++) {
        if (q > q_limit) {
            dc_errf(errbuf, errbuf_len,
                    "radix^min_length (%u^%d) exceeds 2^128", radix,
                    min_length);
            err = DEALCODE_ERR_CONFIG;
            goto wipe_key;
        }
        q = q * radix + (radix - 1);
    }
    if (q < 99) { /* radix^min_length < 100: below FF1's minimum domain */
        dc_errf(errbuf, errbuf_len,
                "radix^min_length (%u^%d) is below FF1's minimum code "
                "space of 100",
                radix, min_length);
        err = DEALCODE_ERR_CONFIG;
        goto wipe_key;
    }
    const u128 q_min = q;

    int max_length = cfg->max_length == 0
                         ? dc_default_max_length(radix, min_length, q_min)
                         : cfg->max_length;
    if (max_length < min_length) {
        dc_errf(errbuf, errbuf_len, "max_length %d < min_length %d",
                max_length, min_length);
        err = DEALCODE_ERR_CONFIG;
        goto wipe_key;
    }
    if (max_length > DC_MAX_LENGTH) {
        dc_errf(errbuf, errbuf_len, "max_length %d > %d", max_length,
                DC_MAX_LENGTH);
        err = DEALCODE_ERR_CONFIG;
        goto wipe_key;
    }

    const char *domain = cfg->domain == NULL ? "" : cfg->domain;
    const size_t domain_len = strlen(domain);
    if (domain_len > DC_MAX_DOMAIN) {
        dc_errf(errbuf, errbuf_len,
                "domain exceeds %d UTF-8 bytes (got %zu)", DC_MAX_DOMAIN,
                domain_len);
        err = DEALCODE_ERR_CONFIG;
        goto wipe_key;
    }
    if (!dc_valid_utf8((const uint8_t *)domain, domain_len)) {
        dc_errf(errbuf, errbuf_len, "domain: not valid UTF-8");
        err = DEALCODE_ERR_CONFIG;
        goto wipe_key;
    }

    dealcode_t *dc = calloc(1, sizeof *dc);
    if (dc == NULL) {
        dc_errf(errbuf, errbuf_len, "out of memory");
        err = DEALCODE_ERR_NOMEM;
        goto wipe_key;
    }

    /* powers[d] = radix^d for d <= max_length - 1, and validate
     * radix^max_length <= 2^128 (q stays representable). */
    q = q_min;
    dc->powers[0] = 1;
    for (int d = 1; d < min_length; d++)
        dc->powers[d] = dc->powers[d - 1] * radix;
    for (int d = min_length; d < max_length; d++) {
        dc->powers[d] = q + 1; /* radix^d, d <= max_length-1: representable */
        if (q > q_limit) {
            dc_errf(errbuf, errbuf_len,
                    "radix^max_length (%u^%d) exceeds 2^128", radix,
                    max_length);
            err = DEALCODE_ERR_CONFIG;
            goto fail_free;
        }
        q = q * radix + (radix - 1);
    }
    /* here q == radix^max_length - 1 (when max_length == min_length the
     * loop above did not run and q is still radix^min_length - 1) */
    dc->capacity = (q >= TWO63 - 1) ? (uint64_t)TWO63 : (uint64_t)(q + 1);

    err = dealcode_ff1_init(&dc->ff1, aes_key, aes_key_len, radix);
    if (err != DEALCODE_OK) {
        dc_errf(errbuf, errbuf_len, "internal: FF1 initialization failed");
        goto fail_free;
    }

    memcpy(dc->alphabet, chars, radix);
    dc->alphabet[radix] = '\0';
    dc->radix = radix;
    dc->min_length = min_length;
    dc->max_length = max_length;
    dc->norm = norm;
    for (int i = 0; i < 256; i++)
        dc->index[i] = -1;
    for (unsigned i = 0; i < radix; i++)
        dc->index[(unsigned char)chars[i]] = (int16_t)i;
    memcpy(dc->tweak, DC_TWEAK_PREFIX, DC_TWEAK_PREFIX_LEN);
    memcpy(dc->tweak + DC_TWEAK_PREFIX_LEN, domain, domain_len);
    dc->tweak_len = DC_TWEAK_PREFIX_LEN + domain_len;

    OPENSSL_cleanse(aes_key, sizeof aes_key);
    *out = dc;
    return DEALCODE_OK;

fail_free:
    OPENSSL_cleanse(dc, sizeof *dc);
    free(dc);
wipe_key:
    OPENSSL_cleanse(aes_key, sizeof aes_key);
    return err;
}

dealcode_err_t dealcode_new(const dealcode_config_t *cfg, dealcode_t **out)
{
    return dealcode_new_ex(cfg, out, NULL, 0);
}

void dealcode_free(dealcode_t *dc)
{
    if (dc == NULL)
        return;
    OPENSSL_cleanse(dc, sizeof *dc); /* wipe key material */
    free(dc);
}

dealcode_err_t dealcode_encode(const dealcode_t *dc, uint64_t n, char *out,
                               size_t out_size)
{
    if (dc == NULL)
        return DEALCODE_ERR_CONFIG;
    if (n >= dc->capacity)
        return DEALCODE_ERR_RANGE;

    /* stage: smallest d >= min_length with n < radix^d (SPEC.md §4);
     * n < capacity <= radix^max_length guarantees termination at d = max. */
    int d = dc->min_length;
    while (d < dc->max_length && (u128)n >= dc->powers[d])
        d++;

    if (out == NULL || out_size < (size_t)d + 1)
        return DEALCODE_ERR_BUFFER;

    const u128 base = (d == dc->min_length) ? 0 : dc->powers[d - 1];
    const u128 v = (u128)n - base;

    uint8_t plain[DC_MAX_LENGTH] = { 0 }, cipher[DC_MAX_LENGTH] = { 0 };
    ff1_str(v, dc->radix, (size_t)d, plain);
    dealcode_err_t err = dealcode_ff1_encrypt(&dc->ff1, dc->tweak,
                                              dc->tweak_len, plain, (size_t)d,
                                              cipher);
    if (err != DEALCODE_OK)
        return err;
    for (int i = 0; i < d; i++)
        out[i] = dc->alphabet[cipher[i]];
    out[d] = '\0';
    return DEALCODE_OK;
}

dealcode_err_t dealcode_decode(const dealcode_t *dc, const char *code,
                               uint64_t *n_out)
{
    if (dc == NULL || n_out == NULL)
        return DEALCODE_ERR_CONFIG;
    if (code == NULL)
        return DEALCODE_ERR_INVALID_CODE;

    const size_t d = strlen(code);
    if (d < (size_t)dc->min_length || d > (size_t)dc->max_length)
        return DEALCODE_ERR_INVALID_CODE;

    uint8_t cipher[DC_MAX_LENGTH] = { 0 }, plain[DC_MAX_LENGTH] = { 0 };
    for (size_t i = 0; i < d; i++) {
        const char c = dc_norm_char(code[i], dc->norm);
        const int16_t idx = dc->index[(unsigned char)c];
        if (idx < 0)
            return DEALCODE_ERR_INVALID_CODE;
        cipher[i] = (uint8_t)idx;
    }

    dealcode_err_t err = dealcode_ff1_decrypt(&dc->ff1, dc->tweak,
                                              dc->tweak_len, cipher, d, plain);
    if (err != DEALCODE_OK)
        return err;

    const u128 v = ff1_num(plain, d, dc->radix); /* < radix^d <= 2^128 - ok */
    u128 base = 0;
    if (d > (size_t)dc->min_length) {
        base = dc->powers[d - 1];
        /* stage capacity = radix^d - radix^(d-1) = radix^(d-1) * (radix-1);
         * always < 2^128 even when radix^max_length == 2^128 exactly. */
        const u128 stage_capacity = base * (dc->radix - 1);
        if (v >= stage_capacity)
            return DEALCODE_ERR_INVALID_CODE; /* outside the stage */
    }
    const u128 n = base + v; /* < radix^d <= 2^128: no overflow */
    if (n >= TWO63)
        return DEALCODE_ERR_INVALID_CODE; /* outside the counter space */
    *n_out = (uint64_t)n;
    return DEALCODE_OK;
}

uint64_t dealcode_capacity(const dealcode_t *dc)
{
    return dc == NULL ? 0 : dc->capacity;
}

int dealcode_min_length(const dealcode_t *dc)
{
    return dc == NULL ? 0 : dc->min_length;
}

int dealcode_max_length(const dealcode_t *dc)
{
    return dc == NULL ? 0 : dc->max_length;
}

int dealcode_radix(const dealcode_t *dc)
{
    return dc == NULL ? 0 : (int)dc->radix;
}

const char *dealcode_alphabet(const dealcode_t *dc)
{
    return dc == NULL ? NULL : dc->alphabet;
}

const char *dealcode_strerror(dealcode_err_t err)
{
    switch (err) {
    case DEALCODE_OK:
        return "success";
    case DEALCODE_ERR_CONFIG:
        return "invalid configuration";
    case DEALCODE_ERR_RANGE:
        return "counter out of range";
    case DEALCODE_ERR_INVALID_CODE:
        return "invalid code";
    case DEALCODE_ERR_BUFFER:
        return "output buffer too small";
    case DEALCODE_ERR_CRYPTO:
        return "cryptographic backend failure";
    case DEALCODE_ERR_NOMEM:
        return "out of memory";
    default:
        return "unknown error";
    }
}
