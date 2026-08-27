/*
 * ff1.h — private FF1 core (NIST SP 800-38G, Algorithms 7 and 8).
 *
 * This header is NOT part of the public API.  It exists so the test suite
 * can exercise the FF1 core directly against the official NIST sample
 * vectors; applications must use the dealcode_* functions in
 * include/dealcode.h.
 *
 * Numeral strings are arrays of uint8_t values in [0, radix); conversion
 * to/from characters is the caller's concern.
 */

#ifndef DEALCODE_FF1_H
#define DEALCODE_FF1_H

#include <stddef.h>
#include <stdint.h>

#include "dealcode.h" /* dealcode_err_t */

#ifdef __cplusplus
extern "C" {
#endif

/* FF1 cipher state: AES key bytes plus the radix. Immutable after init;
 * safe to share across threads (encrypt/decrypt create a per-call OpenSSL
 * cipher context). */
typedef struct {
    uint8_t key[32];
    size_t key_len; /* 16, 24 or 32 */
    unsigned radix; /* in [2, 256] */
} dealcode_ff1_t;

/* Initialize the FF1 state. key_len must be 16, 24 or 32; radix in
 * [2, 256]. Returns DEALCODE_OK or DEALCODE_ERR_CONFIG. */
dealcode_err_t dealcode_ff1_init(dealcode_ff1_t *ff1, const uint8_t *key,
                                 size_t key_len, unsigned radix);

/* FF1.Encrypt (Algorithm 7). x and out are numeral strings of length n
 * (they may alias). Constraints: 2 <= n <= 128, radix^n >= 100, and
 * radix^ceil(n/2) < 2^96 (always true within dealcode's configuration
 * bounds, where radix^ceil(n/2) < 2^68). */
dealcode_err_t dealcode_ff1_encrypt(const dealcode_ff1_t *ff1,
                                    const uint8_t *tweak, size_t tweak_len,
                                    const uint8_t *x, size_t n, uint8_t *out);

/* FF1.Decrypt (Algorithm 8). Same constraints as dealcode_ff1_encrypt. */
dealcode_err_t dealcode_ff1_decrypt(const dealcode_ff1_t *ff1,
                                    const uint8_t *tweak, size_t tweak_len,
                                    const uint8_t *x, size_t n, uint8_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DEALCODE_FF1_H */
