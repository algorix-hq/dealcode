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
 *     std::string code = codec.encode(42);          // e.g. "4b71b7"
 *     uint64_t n = codec.decode(code);              // 42
 * @endcode
 *
 * Semantics:
 *  - Codec is move-only (it owns the underlying C handle via unique_ptr).
 *  - All const member functions are thread-safe; a Codec may be shared
 *    across threads once constructed.
 *  - Errors are thrown as exceptions: ConfigError, RangeError,
 *    InvalidCodeError (all deriving from dealcode::Error, which derives
 *    from std::runtime_error).
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

[[noreturn]] inline void throw_error(dealcode_err_t err,
                                     const std::string &context)
{
    const std::string what = context + ": " + dealcode_strerror(err);
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
        if (err != DEALCODE_OK)
            detail::throw_error(err, "decode(\"" + code_copy + "\")");
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

    /** The alphabet characters, in numeral order. */
    std::string_view alphabet() const noexcept
    {
        return dealcode_alphabet(handle_.get());
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
        const dealcode_err_t err = dealcode_new(&cfg, &dc);
        if (err != DEALCODE_OK)
            detail::throw_error(err, "Codec()");
        handle_.reset(dc);
    }

    std::unique_ptr<dealcode_t, detail::Deleter> handle_;
};

} // namespace dealcode

#endif /* DEALCODE_HPP */
