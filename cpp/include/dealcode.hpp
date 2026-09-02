/**
 * @file dealcode.hpp
 * @brief dealcode — C++17 wrapper over the C core (see ../../SPEC.md).
 *
 * Header-only wrapper around the dealcode C library. The algorithm lives
 * entirely in the C core (c/src/dealcode.c); this header adds RAII,
 * exceptions, and std::string ergonomics.
 *
 * Usage:
 * @code
 *     dealcode::Options opts;
 *     opts.alphabet = "hex";
 *     opts.domain = "orders";
 *     dealcode::Codec codec("example-key", opts);   // string key rule
 *
 *     std::string code = codec.encode(42);          // e.g. "59e5f2"
 *     uint64_t n = codec.decode(code);              // 42
 * @endcode
 *
 * Fixed-length cycling mode (SPEC.md §11) is wrapped by
 * dealcode::CycleCodec / dealcode::CycleOptions, and integer range mode
 * (SPEC.md §12) by dealcode::RangeCodec / dealcode::RangeOptions, with the
 * same semantics.
 *
 * Semantics:
 *  - Codec is move-only (it owns the underlying C handle via unique_ptr).
 *  - All const member functions are thread-safe; a Codec may be shared
 *    across threads once constructed.
 *  - Errors are thrown as exceptions: ConfigError, RangeError,
 *    InvalidCodeError (all deriving from dealcode::Error, which derives
 *    from std::runtime_error). Construction failures carry the C core's
 *    field-level diagnostic (via dealcode_new_ex) in what(), e.g.
 *    "Codec(): alphabet: duplicate character 'a'".
 *
 * Link requirement: the dealcode C core and OpenSSL libcrypto
 * (the provided CMakeLists.txt handles both).
 */

#ifndef DEALCODE_HPP
#define DEALCODE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "dealcode.h"

namespace dealcode {

/** Base class for all dealcode exceptions. */
class Error : public std::runtime_error {
public:
    explicit Error(const std::string &what) : std::runtime_error(what) {}
};

/** Invalid configuration (key, alphabet, lengths, domain). */
class ConfigError : public Error {
public:
    explicit ConfigError(const std::string &what) : Error(what) {}
};

/** encode() called with a counter outside [0, capacity()). */
class RangeError : public Error {
public:
    explicit RangeError(const std::string &what) : Error(what) {}
};

/** decode() input fails length, charset, or stage-range validation. */
class InvalidCodeError : public Error {
public:
    explicit InvalidCodeError(const std::string &what) : Error(what) {}
};

/** Codec configuration (SPEC.md §2); key material is passed separately. */
struct Options {
    /** Preset name ("dec", "hex", "base32", "crockford", "base36",
     * "base58", "base62", "base64url") or a custom alphabet of 2-94
     * distinct printable ASCII characters. */
    std::string alphabet = "hex";
    /** Minimum code length; must satisfy min_length >= 2 and
     * radix^min_length >= 100. */
    int min_length = 6;
    /** Maximum code length; std::nullopt selects the spec default
     * (largest L with radix^L <= 2^63 - 1). */
    std::optional<int> max_length;
    /** Namespace label bound into the FF1 tweak ("dealcode/v1/" + domain);
     * at most 255 UTF-8 bytes. */
    std::string domain;
};

namespace detail {

struct Deleter {
    void operator()(dealcode_t *dc) const noexcept { dealcode_free(dc); }
};

[[noreturn]] inline void throw_error_what(dealcode_err_t err,
                                          const std::string &what)
{
    switch (err) {
    case DEALCODE_ERR_CONFIG:
        throw ConfigError(what);
    case DEALCODE_ERR_RANGE:
        throw RangeError(what);
    case DEALCODE_ERR_INVALID_CODE:
        throw InvalidCodeError(what);
    case DEALCODE_ERR_NOMEM:
        throw std::bad_alloc();
    default:
        throw Error(what);
    }
}

[[noreturn]] inline void throw_error(dealcode_err_t err,
                                     const std::string &context)
{
    throw_error_what(err, context + ": " + dealcode_strerror(err));
}

/* The C API takes NUL-terminated strings, so an embedded U+0000 cannot be
 * passed through. Reject it explicitly rather than silently truncating at
 * the NUL (SPEC.md §8: implementations MUST NOT silently truncate). */
inline void reject_embedded_nul(std::string_view value, const char *what,
                                bool as_invalid_code = false)
{
    if (value.find('\0') == std::string_view::npos)
        return;
    const std::string msg = std::string(what) + " must not contain U+0000";
    if (as_invalid_code)
        throw InvalidCodeError(msg);
    throw ConfigError(msg);
}

} // namespace detail

/**
 * Bijective counter <-> code mapping. Thin RAII wrapper over dealcode_t.
 *
 * Move-only: copying is disabled because the handle owns key material.
 * Immutable and safe for concurrent use once constructed.
 */
class Codec {
public:
    /** Construct with string key material (SPEC.md §2.1 string rule:
     * always derived via SHA-256("dealcode/v1/kdf" || utf8)). */
    explicit Codec(std::string_view key, Options opts = {})
    {
        detail::reject_embedded_nul(key, "string key material");
        const std::string key_copy(key); /* ensure NUL termination */
        dealcode_config_t cfg{};
        cfg.key_string = key_copy.c_str();
        init(cfg, opts);
    }

    /** Construct with byte key material (SPEC.md §2.1 bytes rule: 16/24/32
     * bytes are used directly as the AES key, other lengths are derived). */
    Codec(const std::uint8_t *key, std::size_t key_len, Options opts = {})
    {
        dealcode_config_t cfg{};
        cfg.key = key;
        cfg.key_len = key_len;
        init(cfg, opts);
    }

    /** Convenience overload of the bytes rule. */
    explicit Codec(const std::vector<std::uint8_t> &key, Options opts = {})
        : Codec(key.data(), key.size(), std::move(opts))
    {
    }

    Codec(Codec &&) noexcept = default;
    Codec &operator=(Codec &&) noexcept = default;
    Codec(const Codec &) = delete;
    Codec &operator=(const Codec &) = delete;

    /** Map counter `n` to its code. Throws RangeError if
     * n >= capacity(). */
    std::string encode(std::uint64_t n) const
    {
        char buf[DEALCODE_MAX_CODE_SIZE];
        const dealcode_err_t err =
            dealcode_encode(handle_.get(), n, buf, sizeof buf);
        if (err != DEALCODE_OK)
            detail::throw_error(err, "encode(" + std::to_string(n) + ")");
        return std::string(buf);
    }

    /** Map a code back to its counter. Throws InvalidCodeError if the code
     * fails length/charset/stage-range checks. */
    std::uint64_t decode(std::string_view code) const
    {
        detail::reject_embedded_nul(code, "code", /*as_invalid_code=*/true);
        const std::string code_copy(code); /* ensure NUL termination */
        std::uint64_t n = 0;
        const dealcode_err_t err =
            dealcode_decode(handle_.get(), code_copy.c_str(), &n);
        if (err != DEALCODE_OK) {
            /* Echo at most a prefix of the (attacker-controlled) input so
             * what() stays small for oversized garbage. */
            constexpr std::size_t kEcho = 64;
            std::string shown = code_copy.substr(0, kEcho);
            if (code_copy.size() > kEcho)
                shown += "…(" + std::to_string(code_copy.size()) + " bytes)";
            detail::throw_error(err, "decode(\"" + shown + "\")");
        }
        return n;
    }

    /** Number of encodable counters: min(radix^max_length, 2^63). */
    std::uint64_t capacity() const noexcept
    {
        return dealcode_capacity(handle_.get());
    }

    /** Configured minimum code length. */
    int min_length() const noexcept
    {
        return dealcode_min_length(handle_.get());
    }

    /** Configured maximum code length. */
    int max_length() const noexcept
    {
        return dealcode_max_length(handle_.get());
    }

    /** Alphabet size (number of characters). */
    int radix() const noexcept { return dealcode_radix(handle_.get()); }

    /** The alphabet characters, in numeral order (empty if moved-from). */
    std::string_view alphabet() const noexcept
    {
        const char *chars = dealcode_alphabet(handle_.get());
        return chars ? std::string_view(chars) : std::string_view();
    }

private:
    void init(dealcode_config_t &cfg, const Options &opts)
    {
        detail::reject_embedded_nul(opts.alphabet, "alphabet");
        detail::reject_embedded_nul(opts.domain, "domain");
        cfg.alphabet = opts.alphabet.c_str();
        cfg.min_length = opts.min_length;
        if (opts.max_length.has_value()) {
            if (*opts.max_length == 0)
                throw ConfigError("max_length must not be 0; omit it to use "
                                  "the default");
            cfg.max_length = *opts.max_length;
        } else {
            cfg.max_length = 0; /* C API: 0 selects the spec default */
        }
        cfg.domain = opts.domain.c_str();
        if (opts.min_length == 0)
            throw ConfigError("min_length must be >= 2");

        dealcode_t *dc = nullptr;
        char errbuf[DEALCODE_ERRBUF_SIZE];
        const dealcode_err_t err =
            dealcode_new_ex(&cfg, &dc, errbuf, sizeof errbuf);
        if (err != DEALCODE_OK) {
            /* Prefer the C core's field-level diagnostic (e.g. "alphabet:
             * duplicate character 'a'") over the generic strerror text. */
            const std::string detail =
                errbuf[0] != '\0' ? errbuf : dealcode_strerror(err);
            detail::throw_error_what(err, "Codec(): " + detail);
        }
        handle_.reset(dc);
    }

    std::unique_ptr<dealcode_t, detail::Deleter> handle_;
};

/** Cycling-codec configuration (SPEC.md §11.1); key material is passed
 * separately. Alphabet and domain follow the same rules as Options. */
struct CycleOptions {
    /** Preset name or a custom alphabet, as in Options. */
    std::string alphabet = "hex";
    /** Fixed code length; must satisfy 2 <= length <= 128,
     * radix^length >= 100 and radix^length <= 2^63 (the per-cycle capacity
     * must itself fit the counter space; for larger fixed spaces use Codec
     * with min_length == max_length). */
    int length = 6;
    /** Namespace label bound into the FF1 tweak
     * ("dealcode/v1c/" + cycle + "/" + domain); at most 255 UTF-8 bytes. */
    std::string domain;
};

namespace detail {

struct CycleDeleter {
    void operator()(dealcode_cycle_t *dc) const noexcept
    {
        dealcode_cycle_free(dc);
    }
};

} // namespace detail

/**
 * Fixed-length cycling codec (SPEC.md §11). Thin RAII wrapper over
 * dealcode_cycle_t, with the same move-only/exception/thread-safety
 * semantics as Codec.
 *
 * Codes are always exactly length() characters. Counter `n` belongs to
 * cycle `n / capacity()` with in-cycle value `n % capacity()`; every cycle
 * is a different keyed permutation of the same code space (a different FF1
 * tweak), so when the space is exhausted it refills in a new order instead
 * of growing.
 *
 * Codes REPEAT across cycles by design — keep at most one cycle's codes
 * live per uniqueness scope (`UNIQUE(cycle, code)`, not `UNIQUE(code)`),
 * and store which cycle a live code belongs to: decode() needs it.
 */
class CycleCodec {
public:
    /** Construct with string key material (SPEC.md §2.1 string rule). */
    explicit CycleCodec(std::string_view key, CycleOptions opts = {})
    {
        detail::reject_embedded_nul(key, "string key material");
        const std::string key_copy(key); /* ensure NUL termination */
        dealcode_cycle_config_t cfg{};
        cfg.key_string = key_copy.c_str();
        init(cfg, opts);
    }

    /** Construct with byte key material (SPEC.md §2.1 bytes rule). */
    CycleCodec(const std::uint8_t *key, std::size_t key_len,
               CycleOptions opts = {})
    {
        dealcode_cycle_config_t cfg{};
        cfg.key = key;
        cfg.key_len = key_len;
        init(cfg, opts);
    }

    /** Convenience overload of the bytes rule. */
    explicit CycleCodec(const std::vector<std::uint8_t> &key,
                        CycleOptions opts = {})
        : CycleCodec(key.data(), key.size(), std::move(opts))
    {
    }

    CycleCodec(CycleCodec &&) noexcept = default;
    CycleCodec &operator=(CycleCodec &&) noexcept = default;
    CycleCodec(const CycleCodec &) = delete;
    CycleCodec &operator=(const CycleCodec &) = delete;

    /** Map counter `n` to its fixed-length code
     * (cycle = n / capacity()). Throws RangeError if n >= 2^63. */
    std::string encode(std::uint64_t n) const
    {
        char buf[DEALCODE_MAX_CODE_SIZE];
        const dealcode_err_t err =
            dealcode_cycle_encode(handle_.get(), n, buf, sizeof buf);
        if (err != DEALCODE_OK)
            detail::throw_error(err, "encode(" + std::to_string(n) + ")");
        return std::string(buf);
    }

    /** Map a code issued in `cycle` back to its counter. The cycle is
     * required: the same string recurs in every cycle, mapping to a
     * different counter each time (SPEC.md §11.3). Throws RangeError if
     * cycle > max_cycle(); InvalidCodeError if the code fails
     * length/charset checks or maps past 2^63 in the final partial
     * cycle. */
    std::uint64_t decode(std::string_view code, std::uint64_t cycle) const
    {
        detail::reject_embedded_nul(code, "code", /*as_invalid_code=*/true);
        const std::string code_copy(code); /* ensure NUL termination */
        std::uint64_t n = 0;
        const dealcode_err_t err = dealcode_cycle_decode(
            handle_.get(), code_copy.c_str(), cycle, &n);
        if (err != DEALCODE_OK) {
            /* Echo at most a prefix of the (attacker-controlled) input so
             * what() stays small for oversized garbage. */
            constexpr std::size_t kEcho = 64;
            std::string shown = code_copy.substr(0, kEcho);
            if (code_copy.size() > kEcho)
                shown += "…(" + std::to_string(code_copy.size()) + " bytes)";
            detail::throw_error(err, "decode(\"" + shown + "\", " +
                                         std::to_string(cycle) + ")");
        }
        return n;
    }

    /** Codes per cycle: radix^length (may be exactly 2^63). */
    std::uint64_t capacity() const noexcept
    {
        return dealcode_cycle_capacity(handle_.get());
    }

    /** Largest usable cycle: (2^63 - 1) / capacity(). */
    std::uint64_t max_cycle() const noexcept
    {
        return dealcode_cycle_max_cycle(handle_.get());
    }

    /** Configured fixed code length. */
    int length() const noexcept
    {
        return dealcode_cycle_length(handle_.get());
    }

    /** Alphabet size (number of characters). */
    int radix() const noexcept
    {
        return dealcode_cycle_radix(handle_.get());
    }

    /** The alphabet characters, in numeral order (empty if moved-from). */
    std::string_view alphabet() const noexcept
    {
        const char *chars = dealcode_cycle_alphabet(handle_.get());
        return chars ? std::string_view(chars) : std::string_view();
    }

private:
    void init(dealcode_cycle_config_t &cfg, const CycleOptions &opts)
    {
        detail::reject_embedded_nul(opts.alphabet, "alphabet");
        detail::reject_embedded_nul(opts.domain, "domain");
        cfg.alphabet = opts.alphabet.c_str();
        cfg.length = opts.length;
        cfg.domain = opts.domain.c_str();
        if (opts.length == 0)
            throw ConfigError("length must be >= 2");

        dealcode_cycle_t *dc = nullptr;
        char errbuf[DEALCODE_ERRBUF_SIZE];
        const dealcode_err_t err =
            dealcode_cycle_new_ex(&cfg, &dc, errbuf, sizeof errbuf);
        if (err != DEALCODE_OK) {
            /* Prefer the C core's field-level diagnostic. */
            const std::string detail =
                errbuf[0] != '\0' ? errbuf : dealcode_strerror(err);
            detail::throw_error_what(err, "CycleCodec(): " + detail);
        }
        handle_.reset(dc);
    }

    std::unique_ptr<dealcode_cycle_t, detail::CycleDeleter> handle_;
};

/** Range-codec configuration (SPEC.md §12.1); key material is passed
 * separately. `low` and `high` are required (the {0, 0} defaults fail
 * construction: the range must span at least 100 values). */
struct RangeOptions {
    /** Smallest code value (inclusive). */
    std::uint64_t low = 0;
    /** Largest range value (inclusive); must be <= 2^63 - 1. Codes above
     * low + capacity - 1 (the *dead zone*) are never issued. */
    std::uint64_t high = 0;
    /** Namespace label bound into the FF1 tweak
     * ("dealcode/v1r/" + low + "/" + high + "/" + domain); at most 255
     * UTF-8 bytes. */
    std::string domain;
};

namespace detail {

struct RangeDeleter {
    void operator()(dealcode_range_t *dc) const noexcept
    {
        dealcode_range_free(dc);
    }
};

} // namespace detail

/**
 * Integer range codec (SPEC.md §12). Thin RAII wrapper over
 * dealcode_range_t, with the same move-only/exception/thread-safety
 * semantics as Codec.
 *
 * Codes are **integers** drawn without repetition from [low, high] — built
 * for ranges like [100000, 999999]: every code is a 6-digit number with no
 * leading zero, safe to store in an integer column. Counters
 * 0 <= n < capacity() map bijectively to codes in
 * [low, low + capacity() - 1] through a single FF1 call. capacity() is the
 * largest FF1 domain (radix^m with radix <= 256) that fits in the range,
 * so it can be slightly smaller than high - low + 1; the uncovered top
 * slice is never issued and is rejected by decode().
 */
class RangeCodec {
public:
    /** Construct with string key material (SPEC.md §2.1 string rule). */
    explicit RangeCodec(std::string_view key, RangeOptions opts)
    {
        detail::reject_embedded_nul(key, "string key material");
        const std::string key_copy(key); /* ensure NUL termination */
        dealcode_range_config_t cfg{};
        cfg.key_string = key_copy.c_str();
        init(cfg, opts);
    }

    /** Construct with byte key material (SPEC.md §2.1 bytes rule). */
    RangeCodec(const std::uint8_t *key, std::size_t key_len,
               RangeOptions opts)
    {
        dealcode_range_config_t cfg{};
        cfg.key = key;
        cfg.key_len = key_len;
        init(cfg, opts);
    }

    /** Convenience overload of the bytes rule. */
    RangeCodec(const std::vector<std::uint8_t> &key, RangeOptions opts)
        : RangeCodec(key.data(), key.size(), std::move(opts))
    {
    }

    RangeCodec(RangeCodec &&) noexcept = default;
    RangeCodec &operator=(RangeCodec &&) noexcept = default;
    RangeCodec(const RangeCodec &) = delete;
    RangeCodec &operator=(const RangeCodec &) = delete;

    /** Map counter `n` to its integer code in
     * [low(), low() + capacity() - 1]. Throws RangeError if
     * n >= capacity() (this mode has no staging and no cycles; when the
     * range is exhausted, it is exhausted). */
    std::uint64_t encode(std::uint64_t n) const
    {
        std::uint64_t code = 0;
        const dealcode_err_t err =
            dealcode_range_encode(handle_.get(), n, &code);
        if (err != DEALCODE_OK)
            detail::throw_error(err, "encode(" + std::to_string(n) + ")");
        return code;
    }

    /** Map an integer code back to its counter. Throws InvalidCodeError if
     * the code is outside [low(), high()] or in the dead zone
     * [low() + capacity(), high()] — i.e. was never issued by this
     * codec. */
    std::uint64_t decode(std::uint64_t code) const
    {
        std::uint64_t n = 0;
        const dealcode_err_t err =
            dealcode_range_decode(handle_.get(), code, &n);
        if (err != DEALCODE_OK)
            detail::throw_error(err, "decode(" + std::to_string(code) + ")");
        return n;
    }

    /** Number of issuable codes: the largest admissible radix^m
     * <= high - low + 1 (may be exactly 2^63). The effective capacity is
     * this value, not high - low + 1 — monitor counter consumption against
     * it (SPEC.md §12.4). */
    std::uint64_t capacity() const noexcept
    {
        return dealcode_range_capacity(handle_.get());
    }

    /** Configured `low` bound. */
    std::uint64_t low() const noexcept
    {
        return dealcode_range_low(handle_.get());
    }

    /** Configured `high` bound. */
    std::uint64_t high() const noexcept
    {
        return dealcode_range_high(handle_.get());
    }

    /** Internal FF1 radix selected per SPEC.md §12.2; informational. */
    int radix() const noexcept
    {
        return dealcode_range_radix(handle_.get());
    }

private:
    void init(dealcode_range_config_t &cfg, const RangeOptions &opts)
    {
        detail::reject_embedded_nul(opts.domain, "domain");
        cfg.low = opts.low;
        cfg.high = opts.high;
        cfg.domain = opts.domain.c_str();

        dealcode_range_t *dc = nullptr;
        char errbuf[DEALCODE_ERRBUF_SIZE];
        const dealcode_err_t err =
            dealcode_range_new_ex(&cfg, &dc, errbuf, sizeof errbuf);
        if (err != DEALCODE_OK) {
            /* Prefer the C core's field-level diagnostic. */
            const std::string detail =
                errbuf[0] != '\0' ? errbuf : dealcode_strerror(err);
            detail::throw_error_what(err, "RangeCodec(): " + detail);
        }
        handle_.reset(dc);
    }

    std::unique_ptr<dealcode_range_t, detail::RangeDeleter> handle_;
};

} // namespace dealcode

#endif /* DEALCODE_HPP */
