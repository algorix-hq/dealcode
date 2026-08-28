/**
 * @file dealcode.h
 * @brief dealcode — bijective counter <-> code mapping (format version 1).
 *
 * Dealcode maps a non-negative integer counter `n` (from a database sequence
 * or any other never-repeating source) to a short, fixed-alphabet,
 * random-looking string called a *code*, and back.  The mapping is a keyed
 * permutation (FF1 format-preserving encryption per NIST SP 800-38G plus a
 * length-staging layer), so two different counters can never produce the same
 * code, and a code decodes back to its counter for anyone holding the key.
 *
 * See SPEC.md at the repository root for the normative specification.
 *
 * Usage:
 * @code
 *     dealcode_config_t cfg = {0};
 *     cfg.key_string = "example-key";      // or raw bytes via .key/.key_len
 *     cfg.alphabet   = "hex";              // preset name or custom alphabet
 *     cfg.domain     = "orders";           // namespace label
 *
 *     dealcode_t *dc = NULL;
 *     if (dealcode_new(&cfg, &dc) != DEALCODE_OK) { ... }
 *
 *     char code[DEALCODE_MAX_CODE_SIZE];
 *     dealcode_encode(dc, 42, code, sizeof code);   // e.g. "59e5f2"
 *
 *     uint64_t n;
 *     dealcode_decode(dc, code, &n);                // n == 42
 *
 *     dealcode_free(dc);
 * @endcode
 *
 * Thread safety: a `dealcode_t` is immutable after construction and may be
 * shared freely across threads; `dealcode_encode` / `dealcode_decode` are
 * safe to call concurrently on the same handle (each call uses its own
 * OpenSSL cipher context internally).
 *
 * Dependencies: OpenSSL libcrypto (link with `-lcrypto`).  The
 * implementation also requires a compiler providing `unsigned __int128`
 * (GCC or Clang).
 */

#ifndef DEALCODE_H
#define DEALCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Library version (also the dealcode format is version 1; see
 * SPEC.md). Matches the Version field of the installed dealcode.pc.
 */
#define DEALCODE_VERSION "1.0.0"

/**
 * @brief Result codes. All API functions return one of these.
 */
typedef enum {
    DEALCODE_OK = 0,             /**< Success. */
    DEALCODE_ERR_CONFIG,         /**< Invalid configuration (key, alphabet,
                                      lengths, domain) at construction, or a
                                      NULL required argument. */
    DEALCODE_ERR_RANGE,          /**< encode: counter outside
                                      [0, dealcode_capacity()). */
    DEALCODE_ERR_INVALID_CODE,   /**< decode: input fails length, charset, or
                                      stage-range validation — the code was
                                      never issued by this codec. */
    DEALCODE_ERR_BUFFER,         /**< encode: output buffer too small (or
                                      NULL). */
    DEALCODE_ERR_CRYPTO,         /**< Unexpected OpenSSL failure. */
    DEALCODE_ERR_NOMEM           /**< Memory allocation failed. */
} dealcode_err_t;

/**
 * @brief Opaque codec handle. Create with dealcode_new(), destroy with
 * dealcode_free(). Immutable and thread-safe after creation.
 *
 * (The struct tag is `dealcode_st` rather than `dealcode` so that the C++
 * wrapper's `namespace dealcode` can coexist with this header.)
 */
typedef struct dealcode_st dealcode_t;

/**
 * @brief Codec configuration (SPEC.md §2). Zero-initialize, then set fields.
 *
 * Exactly one of `key` (bytes rule) or `key_string` (string rule) must be
 * set (SPEC.md §2.1):
 *  - bytes of length exactly 16, 24 or 32 are used directly as the AES key;
 *  - bytes of any other non-zero length are derived:
 *    AES-256 key = SHA-256("dealcode/v1/kdf" || material);
 *  - a string is *always* derived from its UTF-8 bytes, regardless of length
 *    or content (a hex-looking string is not auto-decoded);
 *  - empty key material is a DEALCODE_ERR_CONFIG;
 *  - a string that ASCII-case-insensitively equals a preset alphabet name
 *    ("dec", "hex", ..., see `alphabet`) is a DEALCODE_ERR_CONFIG — it is
 *    almost certainly a swapped key/alphabet argument (SPEC.md §2.1). Byte
 *    keys are unaffected.
 */
typedef struct {
    const uint8_t *key;        /**< Key material as bytes, or NULL. */
    size_t key_len;            /**< Length of `key` in bytes. */
    const char *key_string;    /**< Key material as a NUL-terminated UTF-8
                                    string, or NULL. */
    const char *alphabet;      /**< Preset name ("dec", "hex", "base32",
                                    "crockford", "base36", "base58", "base62",
                                    "base64url") or a custom alphabet of 2-94
                                    distinct printable ASCII characters
                                    (0x21-0x7E). Preset names win on conflict.
                                    A custom alphabet that is not exactly a
                                    preset name but ASCII-case-insensitively
                                    equals one ("HEX", "Base62", ...) is a
                                    DEALCODE_ERR_CONFIG (SPEC.md §3.2).
                                    NULL means "hex". */
    int min_length;            /**< Minimum code length; 0 means the default
                                    (6). Must satisfy min_length >= 2 and
                                    radix^min_length >= 100. */
    int max_length;            /**< Maximum code length; 0 means the default
                                    (largest L with radix^L <= 2^63 - 1).
                                    Must satisfy min_length <= max_length and
                                    radix^max_length <= 2^128. */
    const char *domain;        /**< Namespace label bound into the FF1 tweak
                                    ("dealcode/v1/" + domain). At most 255
                                    UTF-8 bytes. NULL means "". */
} dealcode_config_t;

/**
 * @brief Codes are never longer than 128 characters (radix >= 2 and
 * radix^max_length <= 2^128 imply max_length <= 128).  A buffer of this size
 * always fits any code plus its NUL terminator.
 */
#define DEALCODE_MAX_CODE_SIZE 129

/**
 * @brief Create a codec.
 *
 * @param cfg Configuration; see dealcode_config_t. Must not be NULL.
 * @param out Receives the new handle on success. Must not be NULL.
 * @return DEALCODE_OK, or DEALCODE_ERR_CONFIG / DEALCODE_ERR_CRYPTO /
 *         DEALCODE_ERR_NOMEM. On error `*out` is set to NULL.
 */
dealcode_err_t dealcode_new(const dealcode_config_t *cfg, dealcode_t **out);

/**
 * @brief Recommended size for the `errbuf` argument of dealcode_new_ex().
 * Every diagnostic the library produces fits in a buffer of this size.
 */
#define DEALCODE_ERRBUF_SIZE 256

/**
 * @brief Create a codec, with a human-readable diagnostic on failure.
 *
 * Behaves exactly like dealcode_new(), and additionally, when `errbuf` is
 * non-NULL and `errbuf_len` > 0:
 *  - on failure, writes a one-line, NUL-terminated explanation naming the
 *    offending field or rule (e.g. "alphabet: duplicate character 'a'",
 *    "min_length 1 < 2", "domain exceeds 255 UTF-8 bytes (got 256)"),
 *    truncated to fit `errbuf_len`;
 *  - on success, writes an empty string.
 *
 * The diagnostic never echoes key material, with one deliberate exception:
 * a string key rejected for being a preset alphabet name is echoed verbatim
 * (it is a preset name, not a secret) to make the swapped-arguments mistake
 * obvious.
 *
 * @param cfg        Configuration; see dealcode_config_t. Must not be NULL.
 * @param out        Receives the new handle on success. Must not be NULL.
 * @param errbuf     Buffer for the diagnostic, or NULL for none.
 * @param errbuf_len Size of `errbuf` in bytes (DEALCODE_ERRBUF_SIZE
 *                   recommended).
 * @return Same as dealcode_new().
 */
dealcode_err_t dealcode_new_ex(const dealcode_config_t *cfg, dealcode_t **out,
                               char *errbuf, size_t errbuf_len);

/**
 * @brief Destroy a codec and wipe its key material. NULL is a no-op.
 */
void dealcode_free(dealcode_t *dc);

/**
 * @brief Map counter `n` to its code.
 *
 * @param dc       Codec handle.
 * @param n        Counter; must be in [0, dealcode_capacity(dc)).
 * @param out      Receives the NUL-terminated code.
 * @param out_size Size of `out` in bytes. The code for counter `n` needs
 *                 its stage length + 1 bytes; DEALCODE_MAX_CODE_SIZE (or
 *                 dealcode_max_length(dc) + 1) always suffices.
 * @return DEALCODE_OK; DEALCODE_ERR_RANGE if `n` is out of range;
 *         DEALCODE_ERR_BUFFER if `out` is NULL or `out_size` is too small
 *         (nothing is written); DEALCODE_ERR_CRYPTO / DEALCODE_ERR_NOMEM on
 *         internal failure.
 */
dealcode_err_t dealcode_encode(const dealcode_t *dc, uint64_t n,
                               char *out, size_t out_size);

/**
 * @brief Map a code back to its counter.
 *
 * Preset-alphabet normalization (SPEC.md §3.1) is applied to the input
 * first (e.g. hex accepts uppercase). Custom alphabets require exact
 * matches.
 *
 * @param dc    Codec handle.
 * @param code  NUL-terminated code string.
 * @param n_out Receives the counter on success.
 * @return DEALCODE_OK; DEALCODE_ERR_INVALID_CODE if the code fails length,
 *         charset, or stage-range checks (i.e. was never issued by this
 *         codec); DEALCODE_ERR_CRYPTO / DEALCODE_ERR_NOMEM on internal
 *         failure.
 */
dealcode_err_t dealcode_decode(const dealcode_t *dc, const char *code,
                               uint64_t *n_out);

/**
 * @brief Number of encodable counters: min(radix^max_length, 2^63).
 * Counters range over [0, capacity). Returns 0 if `dc` is NULL.
 */
uint64_t dealcode_capacity(const dealcode_t *dc);

/** @brief Configured minimum code length. Returns 0 if `dc` is NULL. */
int dealcode_min_length(const dealcode_t *dc);

/** @brief Configured maximum code length. Returns 0 if `dc` is NULL. */
int dealcode_max_length(const dealcode_t *dc);

/** @brief Alphabet size (number of characters). Returns 0 if `dc` is NULL. */
int dealcode_radix(const dealcode_t *dc);

/**
 * @brief The alphabet characters, in numeral order, as a NUL-terminated
 * string owned by the codec. Returns NULL if `dc` is NULL.
 */
const char *dealcode_alphabet(const dealcode_t *dc);

/**
 * @brief Human-readable description of an error code. Never returns NULL.
 */
const char *dealcode_strerror(dealcode_err_t err);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DEALCODE_H */
