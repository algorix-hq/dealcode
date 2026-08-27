// Test suite for the dealcode C++ wrapper.
//
// Covers: the 9 official NIST FF1 sample vectors (via the C core's private
// FF1 seam), every config/vector/invalid-code/normalize case in
// testvectors/v1.json, exception behaviour, move semantics, and roundtrip
// sweeps across stage boundaries.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dealcode.hpp"
#include "ff1.h" // private test seam from the C core
#include "vectors.inc"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                 \
            std::printf(__VA_ARGS__);                                        \
            std::printf("\n");                                               \
        }                                                                    \
    } while (0)

// ------------------------------------------------------------------------

static std::vector<std::uint8_t> hex_decode(const std::string &hex)
{
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> out;
    if (hex.size() % 2 != 0)
        return {};
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return {};
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

static const char NIST_ALPHABET[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static std::vector<std::uint8_t> nist_numerals(const std::string &s)
{
    std::vector<std::uint8_t> out;
    for (char c : s) {
        const char *p = std::strchr(NIST_ALPHABET, c);
        if (p == nullptr || c == '\0')
            return {};
        out.push_back(static_cast<std::uint8_t>(p - NIST_ALPHABET));
    }
    return out;
}

static void test_nist_ff1()
{
    for (std::size_t i = 0; i < TV_NIST_COUNT; i++) {
        const tv_nist_t &tv = TV_NIST[i];
        const auto key = hex_decode(tv.key_hex);
        const auto tweak = hex_decode(tv.tweak_hex);
        const auto pt = nist_numerals(tv.plaintext);
        const auto ct = nist_numerals(tv.ciphertext);
        CHECK(!key.empty() && !pt.empty() && pt.size() == ct.size(),
              "nist %d: bad vector data", tv.sample);

        dealcode_ff1_t ff1;
        CHECK(dealcode_ff1_init(&ff1, key.data(), key.size(), tv.radix) ==
                  DEALCODE_OK,
              "nist %d: init", tv.sample);

        std::vector<std::uint8_t> out(pt.size());
        CHECK(dealcode_ff1_encrypt(&ff1, tweak.data(), tweak.size(),
                                   pt.data(), pt.size(),
                                   out.data()) == DEALCODE_OK &&
                  out == ct,
              "nist %d: encrypt mismatch", tv.sample);
        CHECK(dealcode_ff1_decrypt(&ff1, tweak.data(), tweak.size(),
                                   ct.data(), ct.size(),
                                   out.data()) == DEALCODE_OK &&
                  out == pt,
              "nist %d: decrypt mismatch", tv.sample);
    }
}

// ------------------------------------------------------------------------

static dealcode::Codec make_codec(const tv_config_t &cfg)
{
    dealcode::Options opts;
    opts.alphabet = cfg.alphabet;
    opts.min_length = cfg.min_length;
    opts.max_length = cfg.max_length;
    opts.domain = cfg.domain;
    if (cfg.key_hex != nullptr)
        return dealcode::Codec(hex_decode(cfg.key_hex), opts);
    return dealcode::Codec(std::string_view(cfg.key_string), opts);
}

static void test_v1_vectors()
{
    for (std::size_t i = 0; i < TV_V1_COUNT; i++) {
        const tv_config_t &cfg = TV_V1[i];
        try {
            dealcode::Codec codec = make_codec(cfg);

            CHECK(codec.min_length() == cfg.min_length &&
                      codec.max_length() == cfg.max_length,
                  "v1 %s: length accessors", cfg.name);
            CHECK(static_cast<std::size_t>(codec.radix()) ==
                      codec.alphabet().size(),
                  "v1 %s: radix/alphabet accessors", cfg.name);

            for (std::size_t j = 0; j < cfg.n_vectors; j++) {
                const tv_pair_t &tv = cfg.vectors[j];
                const std::string code = codec.encode(tv.n);
                CHECK(code == tv.code,
                      "v1 %s: encode(%llu) = \"%s\", want \"%s\"", cfg.name,
                      static_cast<unsigned long long>(tv.n), code.c_str(),
                      tv.code);
                CHECK(codec.decode(tv.code) == tv.n,
                      "v1 %s: decode(\"%s\") != %llu", cfg.name, tv.code,
                      static_cast<unsigned long long>(tv.n));
            }

            for (std::size_t j = 0; j < cfg.n_invalid_codes; j++) {
                bool threw = false;
                try {
                    codec.decode(cfg.invalid_codes[j]);
                } catch (const dealcode::InvalidCodeError &) {
                    threw = true;
                }
                CHECK(threw, "v1 %s: decode(\"%s\") must throw "
                             "InvalidCodeError",
                      cfg.name, cfg.invalid_codes[j]);
            }

            for (std::size_t j = 0; j < cfg.n_normalize; j++) {
                CHECK(codec.decode(cfg.normalize[j].input) ==
                          cfg.normalize[j].n,
                      "v1 %s: normalize decode(\"%s\") != %llu", cfg.name,
                      cfg.normalize[j].input,
                      static_cast<unsigned long long>(cfg.normalize[j].n));
            }
        } catch (const std::exception &e) {
            CHECK(false, "v1 %s: unexpected exception: %s", cfg.name,
                  e.what());
        }
    }
}

// ------------------------------------------------------------------------

template <typename Exc, typename Fn>
static bool throws(Fn &&fn)
{
    try {
        fn();
    } catch (const Exc &) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

static void test_exceptions()
{
    // ConfigError cases
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Codec(std::string_view(""));
          }),
          "config: empty string key");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Codec(std::vector<std::uint8_t>{});
          }),
          "config: empty byte key");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.alphabet = "x";
              dealcode::Codec("k", o);
          }),
          "config: bad custom alphabet");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.alphabet = "abca";
              dealcode::Codec("k", o);
          }),
          "config: duplicate custom alphabet chars");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.min_length = 1;
              dealcode::Codec("k", o);
          }),
          "config: min_length 1");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.min_length = 0;
              dealcode::Codec("k", o);
          }),
          "config: min_length 0");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.alphabet = "01";
              o.min_length = 6; // 2^6 = 64 < 100
              dealcode::Codec("k", o);
          }),
          "config: radix^min_length < 100");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.min_length = 8;
              o.max_length = 7;
              dealcode::Codec("k", o);
          }),
          "config: max < min");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.max_length = 33; // 16^33 > 2^128
              dealcode::Codec("k", o);
          }),
          "config: radix^max_length > 2^128");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.max_length = 0;
              dealcode::Codec("k", o);
          }),
          "config: explicit max_length 0");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::Options o;
              o.domain = std::string(256, 'a');
              dealcode::Codec("k", o);
          }),
          "config: domain > 255 bytes");

    // 16^32 == 2^128 exactly must be accepted
    {
        dealcode::Options o;
        o.max_length = 32;
        dealcode::Codec codec("k", o);
        CHECK(codec.capacity() == (UINT64_C(1) << 63),
              "config: 2^128 code space, capacity capped at 2^63");
        const std::uint64_t big = (UINT64_C(1) << 63) - 1;
        CHECK(codec.decode(codec.encode(big)) == big,
              "config: 2^128 code space roundtrip at capacity-1");
    }

    // RangeError
    {
        dealcode::Options o;
        o.alphabet = "dec";
        o.min_length = 4;
        o.max_length = 6;
        dealcode::Codec codec("k", o);
        CHECK(codec.capacity() == 1000000, "range: capacity");
        CHECK(codec.decode(codec.encode(999999)) == 999999,
              "range: capacity-1 ok");
        CHECK(throws<dealcode::RangeError>([&] { codec.encode(1000000); }),
              "range: encode(capacity) throws");
        CHECK(throws<dealcode::RangeError>([&] { codec.encode(UINT64_MAX); }),
              "range: encode(UINT64_MAX) throws");
    }

    // InvalidCodeError
    {
        dealcode::Codec codec("k");
        CHECK(throws<dealcode::InvalidCodeError>([&] { codec.decode(""); }),
              "invalid: empty code");
        CHECK(throws<dealcode::InvalidCodeError>(
                  [&] { codec.decode("zzzzzz"); }),
              "invalid: bad charset");
        CHECK(throws<dealcode::InvalidCodeError>(
                  [&] { codec.decode("00000"); }),
              "invalid: too short");
        CHECK(throws<dealcode::InvalidCodeError>(
                  [&] { codec.decode("0000000000000000"); }),
              "invalid: too long");
        // exception hierarchy: everything derives from dealcode::Error
        CHECK(throws<dealcode::Error>([&] { codec.decode(""); }),
              "invalid: derives from Error");
        CHECK(throws<std::runtime_error>([&] { codec.decode(""); }),
              "invalid: derives from std::runtime_error");
    }
}

static void test_key_rules()
{
    // 32 raw bytes (direct AES-256) vs the same bytes as a hex STRING must
    // differ: strings are always derived, never auto-decoded.
    const std::string hex =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    dealcode::Codec from_bytes(hex_decode(hex));
    dealcode::Codec from_string{std::string_view(hex)};
    CHECK(from_bytes.encode(42) != from_string.encode(42),
          "key: hex string must not be auto-decoded");

    // byte-key of non-AES length is derived, and (unlike a string) differs
    // from the direct interpretation of any prefix
    const std::vector<std::uint8_t> weird = { 1, 2, 3, 4, 5 };
    dealcode::Codec derived(weird);
    CHECK(derived.decode(derived.encode(7)) == 7, "key: derived byte key");

    // same material, same codes
    dealcode::Codec a(std::string_view("same-key"));
    dealcode::Codec b(std::string_view("same-key"));
    CHECK(a.encode(123) == b.encode(123), "key: determinism");
}

static void test_move_semantics()
{
    dealcode::Codec a("move-key");
    const std::string code = a.encode(42);
    dealcode::Codec b = std::move(a);
    CHECK(b.encode(42) == code && b.decode(code) == 42,
          "move: moved-to codec works");
    dealcode::Codec c("other-key");
    c = std::move(b);
    CHECK(c.encode(42) == code, "move: move-assignment works");
}

static void test_accessors_and_defaults()
{
    dealcode::Codec codec("accessor-key");
    CHECK(codec.min_length() == 6, "defaults: min_length");
    CHECK(codec.max_length() == 15, "defaults: hex max_length 15");
    CHECK(codec.radix() == 16, "defaults: radix");
    CHECK(codec.alphabet() == "0123456789abcdef", "defaults: alphabet");
    CHECK(codec.capacity() == UINT64_C(1152921504606846976),
          "defaults: capacity 16^15");

    dealcode::Options o;
    o.alphabet = "base62";
    dealcode::Codec b62("accessor-key", o);
    CHECK(b62.max_length() == 10, "defaults: base62 max_length 10");
}

static void roundtrip_config(const char *name, const std::string &alphabet,
                             int min_length, std::optional<int> max_length,
                             const std::string &domain)
{
    dealcode::Options o;
    o.alphabet = alphabet;
    o.min_length = min_length;
    o.max_length = max_length;
    o.domain = domain;
    dealcode::Codec codec("roundtrip-key", o);

    const std::uint64_t capacity = codec.capacity();
    const std::uint64_t radix = static_cast<std::uint64_t>(codec.radix());

    const std::uint64_t dense = capacity < 2000 ? capacity : 2000;
    for (std::uint64_t n = 0; n < dense; n++) {
        const std::string code = codec.encode(n);
        CHECK(codec.decode(code) == n, "roundtrip %s: n=%llu", name,
              static_cast<unsigned long long>(n));
    }

    // stage boundaries
    std::uint64_t power = 1;
    int d = 0;
    while (d < codec.max_length() && power <= capacity / radix + 1) {
        power *= radix;
        d++;
        if (d < codec.min_length())
            continue;
        for (std::uint64_t n : { power - 1, power, power + 1 }) {
            if (n >= capacity)
                continue;
            CHECK(codec.decode(codec.encode(n)) == n,
                  "roundtrip %s: boundary n=%llu", name,
                  static_cast<unsigned long long>(n));
        }
    }
    CHECK(codec.decode(codec.encode(capacity - 1)) == capacity - 1,
          "roundtrip %s: capacity-1", name);
}

static void test_roundtrips()
{
    roundtrip_config("hex-default", "hex", 6, std::nullopt, "");
    roundtrip_config("dec-4-6", "dec", 4, 6, "pin");
    roundtrip_config("crockford", "crockford", 6, std::nullopt, "tickets");
    roundtrip_config("custom", "BCDFGHJKLMNPQRSTVWXZ", 6, 14, "");
    roundtrip_config("hex-32-fixed", "hex", 32, 32, ""); // 16^32 == 2^128
    roundtrip_config("binary-7-128", "01", 7, 128, "");  // 2^128 code space
}

static void test_counter_bound_rejection()
{
    // Stages above length 64 start at base >= 2^63: always rejected.
    dealcode::Options o;
    o.alphabet = "01";
    o.min_length = 7;
    o.max_length = 128;
    dealcode::Codec codec("bound-key", o);
    CHECK(codec.capacity() == (UINT64_C(1) << 63), "bound: capacity 2^63");
    for (std::size_t len : { 65, 100, 128 }) {
        CHECK(throws<dealcode::InvalidCodeError>(
                  [&] { codec.decode(std::string(len, '0')); }),
              "bound: %zu-char code rejected", len);
    }
}

// ------------------------------------------------------------------------

int main()
{
    test_nist_ff1();
    test_v1_vectors();
    test_exceptions();
    test_key_rules();
    test_move_semantics();
    test_accessors_and_defaults();
    test_roundtrips();
    test_counter_bound_rejection();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
