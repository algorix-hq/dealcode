// Test suite for the dealcode C++ wrapper.
//
// Covers: the 9 official NIST FF1 sample vectors (via the C core's private
// FF1 seam), every config/vector/invalid-code/normalize case in
// testvectors/v1.json, exception behaviour, move semantics, roundtrip
// sweeps across stage boundaries, fixed-length cycling mode (SPEC
// section 11: every case in testvectors/v1c.json plus behaviour tests),
// and integer range mode (SPEC section 12: every case in
// testvectors/v1r.json plus behaviour tests).

#include <algorithm>
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

            for (std::size_t j = 0; j < cfg.n_range_counters; j++) {
                bool threw = false;
                try {
                    codec.encode(cfg.range_counters[j]);
                } catch (const dealcode::RangeError &) {
                    threw = true;
                }
                CHECK(threw, "v1 %s: encode(%llu) must throw RangeError",
                      cfg.name,
                      static_cast<unsigned long long>(cfg.range_counters[j]));
            }
        } catch (const std::exception &e) {
            CHECK(false, "v1 %s: unexpected exception: %s", cfg.name,
                  e.what());
        }
    }
}

static void test_v1_invalid_configs()
{
    for (std::size_t i = 0; i < TV_V1_INVALID_CONFIG_COUNT; i++) {
        const tv_invalid_config_t &tv = TV_V1_INVALID_CONFIGS[i];
        bool threw = false;
        std::string what;
        try {
            dealcode::Options opts;
            opts.alphabet = tv.alphabet;
            if (tv.min_length != 0)
                opts.min_length = tv.min_length;
            if (tv.max_length != 0)
                opts.max_length = tv.max_length;
            if (tv.domain != nullptr)
                opts.domain = tv.domain;
            if (tv.key_hex != nullptr)
                dealcode::Codec(hex_decode(tv.key_hex), opts);
            else
                dealcode::Codec(std::string_view(tv.key_string), opts);
        } catch (const dealcode::ConfigError &e) {
            threw = true;
            what = e.what();
        }
        CHECK(threw, "invalid-config %s: must throw ConfigError", tv.name);
        CHECK(threw && what.size() > std::string("Codec(): ").size(),
              "invalid-config %s: what() must carry a diagnostic", tv.name);
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

// Guard A: custom alphabet ASCII-case-insensitively equal to a preset name
// (but not exactly the preset name) must be rejected; Guard B: a STRING key
// equal (case-insensitively) to a preset name must be rejected. The
// construction exception must carry the C core's diagnostic.
static void test_preset_name_guards()
{
    // Guard A rejections
    for (const char *bad : { "HEX", "Hex", "DEC", "Base62", "CROCKFORD",
                             "Base64Url" }) {
        bool threw = false;
        std::string what;
        try {
            dealcode::Options o;
            o.alphabet = bad;
            dealcode::Codec("guard-key", o);
        } catch (const dealcode::ConfigError &e) {
            threw = true;
            what = e.what();
        }
        CHECK(threw, "guard A: alphabet \"%s\" must throw ConfigError", bad);
        CHECK(threw && what.find("matches the preset name") !=
                           std::string::npos,
              "guard A: diagnostic for \"%s\", got \"%s\"", bad,
              what.c_str());
    }
    // exact wording for the canonical example
    try {
        dealcode::Options o;
        o.alphabet = "HEX";
        dealcode::Codec("guard-key", o);
        CHECK(false, "guard A: \"HEX\" must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "Codec(): custom alphabet \"HEX\" matches the preset name "
                  "\"hex\" — pass \"hex\" for the preset, or a genuinely "
                  "custom alphabet",
              "guard A: exact message, got \"%s\"", e.what());
    }
    // exact preset names still resolve as presets
    {
        dealcode::Options o;
        o.alphabet = "hex";
        dealcode::Codec codec("guard-key", o);
        CHECK(codec.alphabet() == "0123456789abcdef",
              "guard A: exact \"hex\" resolves as preset");
    }
    // genuinely custom near-misses still work
    for (const char *ok : { "HEXA", "xeh", "dce" }) {
        dealcode::Options o;
        o.alphabet = ok;
        dealcode::Codec codec("guard-key", o);
        CHECK(codec.alphabet() == ok,
              "guard A: custom \"%s\" used verbatim", ok);
    }

    // Guard B rejections (string keys only)
    for (const char *bad : { "dec", "hex", "base32", "crockford", "base36",
                             "base58", "base62", "base64url", "HEX",
                             "Crockford" }) {
        CHECK(throws<dealcode::ConfigError>([&] {
                  dealcode::Codec(std::string_view(bad));
              }),
              "guard B: string key \"%s\" must throw ConfigError", bad);
    }
    try {
        dealcode::Codec codec(std::string_view("crockford"));
        CHECK(false, "guard B: \"crockford\" must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "Codec(): string key \"crockford\" is a preset alphabet "
                  "name — did you swap the key and alphabet fields?",
              "guard B: exact message, got \"%s\"", e.what());
    }
    // near-miss string keys are fine
    for (const char *ok : { "crockford1", "hex ", "base-62" }) {
        dealcode::Codec codec{std::string_view(ok)};
        CHECK(codec.decode(codec.encode(5)) == 5,
              "guard B: string key \"%s\" accepted", ok);
    }
    // byte keys spelling a preset name are unaffected
    {
        const char *name = "crockford";
        dealcode::Codec codec(reinterpret_cast<const std::uint8_t *>(name),
                              9);
        CHECK(codec.decode(codec.encode(5)) == 5,
              "guard B: byte key \"crockford\" accepted");
    }
}

// Construction failures must surface the C core's field-level diagnostics.
static void test_error_details()
{
    try {
        dealcode::Options o;
        o.alphabet = "abca";
        dealcode::Codec("k", o);
        CHECK(false, "detail: duplicate alphabet char must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "Codec(): alphabet: duplicate character 'a'",
              "detail: duplicate char, got \"%s\"", e.what());
    }
    try {
        dealcode::Options o;
        o.min_length = 1;
        dealcode::Codec("k", o);
        CHECK(false, "detail: min_length 1 must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) == "Codec(): min_length 1 < 2",
              "detail: min_length, got \"%s\"", e.what());
    }
    try {
        dealcode::Options o;
        o.domain = std::string(256, 'a');
        dealcode::Codec("k", o);
        CHECK(false, "detail: oversized domain must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "Codec(): domain exceeds 255 UTF-8 bytes (got 256)",
              "detail: domain, got \"%s\"", e.what());
    }
    try {
        dealcode::Codec codec(std::string_view(""));
        CHECK(false, "detail: empty key must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) == "Codec(): key: empty",
              "detail: empty key, got \"%s\"", e.what());
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


// ------------------------------------------------------------------------
// Fixed-length cycling mode (SPEC section 11)
// ------------------------------------------------------------------------

static dealcode::CycleCodec make_cycle_codec(const tv_cycle_config_t &cfg)
{
    dealcode::CycleOptions opts;
    opts.alphabet = cfg.alphabet;
    opts.length = cfg.length;
    opts.domain = cfg.domain;
    if (cfg.key_hex != nullptr)
        return dealcode::CycleCodec(hex_decode(cfg.key_hex), opts);
    return dealcode::CycleCodec(std::string_view(cfg.key_string), opts);
}

static void test_v1c_vectors()
{
    for (std::size_t i = 0; i < TV_V1C_COUNT; i++) {
        const tv_cycle_config_t &cfg = TV_V1C[i];
        try {
            dealcode::CycleCodec codec = make_cycle_codec(cfg);

            CHECK(codec.capacity() == cfg.capacity,
                  "v1c %s: capacity %llu, want %llu", cfg.name,
                  static_cast<unsigned long long>(codec.capacity()),
                  static_cast<unsigned long long>(cfg.capacity));
            CHECK(codec.max_cycle() == cfg.max_cycle,
                  "v1c %s: max_cycle %llu, want %llu", cfg.name,
                  static_cast<unsigned long long>(codec.max_cycle()),
                  static_cast<unsigned long long>(cfg.max_cycle));
            CHECK(codec.length() == cfg.length, "v1c %s: length accessor",
                  cfg.name);
            CHECK(static_cast<std::size_t>(codec.radix()) ==
                      codec.alphabet().size(),
                  "v1c %s: radix/alphabet accessors", cfg.name);

            for (std::size_t j = 0; j < cfg.n_vectors; j++) {
                const tv_pair_t &tv = cfg.vectors[j];
                const std::string code = codec.encode(tv.n);
                CHECK(code == tv.code,
                      "v1c %s: encode(%llu) = \"%s\", want \"%s\"", cfg.name,
                      static_cast<unsigned long long>(tv.n), code.c_str(),
                      tv.code);
                CHECK(codec.decode(tv.code, tv.n / cfg.capacity) == tv.n,
                      "v1c %s: decode(\"%s\", %llu) != %llu", cfg.name,
                      tv.code,
                      static_cast<unsigned long long>(tv.n / cfg.capacity),
                      static_cast<unsigned long long>(tv.n));
            }

            for (std::size_t j = 0; j < cfg.n_invalid_codes; j++) {
                const tv_cycle_code_t &bad = cfg.invalid_codes[j];
                CHECK(throws<dealcode::InvalidCodeError>(
                          [&] { codec.decode(bad.code, bad.cycle); }),
                      "v1c %s: decode(\"%s\", %llu) must throw "
                      "InvalidCodeError",
                      cfg.name, bad.code,
                      static_cast<unsigned long long>(bad.cycle));
            }

            for (std::size_t j = 0; j < cfg.n_normalize; j++) {
                const tv_cycle_norm_t &nc = cfg.normalize[j];
                CHECK(codec.decode(nc.input, nc.cycle) == nc.n,
                      "v1c %s: normalize decode(\"%s\", %llu) != %llu",
                      cfg.name, nc.input,
                      static_cast<unsigned long long>(nc.cycle),
                      static_cast<unsigned long long>(nc.n));
            }

            for (std::size_t j = 0; j < cfg.n_range_counters; j++) {
                CHECK(throws<dealcode::RangeError>(
                          [&] { codec.encode(cfg.range_counters[j]); }),
                      "v1c %s: encode(%llu) must throw RangeError", cfg.name,
                      static_cast<unsigned long long>(cfg.range_counters[j]));
            }

            // invalid cycles ("-1" is unrepresentable in uint64_t and
            // skipped by the generator; max_cycle + 1 is always present)
            const std::string probe = codec.encode(0);
            for (std::size_t j = 0; j < cfg.n_invalid_cycles; j++) {
                CHECK(throws<dealcode::RangeError>(
                          [&] {
                              codec.decode(probe, cfg.invalid_cycles[j]);
                          }),
                      "v1c %s: decode(probe, %llu) must throw RangeError",
                      cfg.name,
                      static_cast<unsigned long long>(cfg.invalid_cycles[j]));
            }
            CHECK(throws<dealcode::RangeError>(
                      [&] { codec.decode(probe, codec.max_cycle() + 1); }),
                  "v1c %s: decode(probe, max_cycle + 1) must throw",
                  cfg.name);
        } catch (const std::exception &e) {
            CHECK(false, "v1c %s: unexpected exception: %s", cfg.name,
                  e.what());
        }
    }
}

static void test_v1c_invalid_configs()
{
    for (std::size_t i = 0; i < TV_V1C_INVALID_CONFIG_COUNT; i++) {
        const tv_cycle_invalid_config_t &tv = TV_V1C_INVALID_CONFIGS[i];
        bool threw = false;
        std::string what;
        try {
            dealcode::CycleOptions opts;
            opts.alphabet = tv.alphabet;
            opts.length = tv.length;
            if (tv.domain != nullptr)
                opts.domain = tv.domain;
            if (tv.key_hex != nullptr)
                dealcode::CycleCodec(hex_decode(tv.key_hex), opts);
            else
                dealcode::CycleCodec(std::string_view(tv.key_string), opts);
        } catch (const dealcode::ConfigError &e) {
            threw = true;
            what = e.what();
        }
        CHECK(threw, "v1c invalid-config %s: must throw ConfigError",
              tv.name);
        CHECK(threw && what.size() > std::string("CycleCodec(): ").size(),
              "v1c invalid-config %s: what() must carry a diagnostic",
              tv.name);
    }
}

// Cycling construction reuses the plain guards verbatim; check the exact
// diagnostics (prefixed CycleCodec()), plus the cycling-only length rules.
static void test_cycle_config_errors()
{
    try {
        dealcode::CycleOptions o;
        o.alphabet = "HEX";
        dealcode::CycleCodec("guard-key", o);
        CHECK(false, "cycle guard A: \"HEX\" must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "CycleCodec(): custom alphabet \"HEX\" matches the preset "
                  "name \"hex\" — pass \"hex\" for the preset, or a "
                  "genuinely custom alphabet",
              "cycle guard A: exact message, got \"%s\"", e.what());
    }
    try {
        dealcode::CycleCodec codec(std::string_view("crockford"));
        CHECK(false, "cycle guard B: \"crockford\" must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "CycleCodec(): string key \"crockford\" is a preset "
                  "alphabet name — did you swap the key and alphabet "
                  "fields?",
              "cycle guard B: exact message, got \"%s\"", e.what());
    }
    // byte keys spelling a preset name are unaffected
    {
        const char *name = "crockford";
        dealcode::CycleCodec codec(
            reinterpret_cast<const std::uint8_t *>(name), 9);
        CHECK(codec.decode(codec.encode(5), 0) == 5,
              "cycle guard B: byte key \"crockford\" accepted");
    }

    // length rules
    try {
        dealcode::CycleOptions o;
        o.length = 1;
        dealcode::CycleCodec("k", o);
        CHECK(false, "cycle: length 1 must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) == "CycleCodec(): length 1 < 2",
              "cycle: length 1 message, got \"%s\"", e.what());
    }
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::CycleOptions o;
              o.length = 0;
              dealcode::CycleCodec("k", o);
          }),
          "cycle: explicit length 0");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::CycleOptions o;
              o.length = 129;
              dealcode::CycleCodec("k", o);
          }),
          "cycle: length 129");
    try {
        dealcode::CycleOptions o;
        o.length = 16; // hex: 16^16 = 2^64 > 2^63
        dealcode::CycleCodec("k", o);
        CHECK(false, "cycle: 16^16 must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "CycleCodec(): radix^length (16^16) exceeds 2^63 in "
                  "cycling mode — a cycle must be completable; use "
                  "min_length == max_length for larger fixed spaces",
              "cycle: over-2^63 message, got \"%s\"", e.what());
    }
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::CycleOptions o;
              o.alphabet = "abcdefghi"; // 9^2 = 81 < 100
              o.length = 2;
              dealcode::CycleCodec("k", o);
          }),
          "cycle: radix^length < 100");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::CycleOptions o;
              o.domain = std::string(256, 'a');
              dealcode::CycleCodec("k", o);
          }),
          "cycle: domain > 255 bytes");

    // embedded NUL rejection, as in Codec
    CHECK(throws<dealcode::ConfigError>(
              [] { dealcode::CycleCodec c(std::string("ab\0cd", 5)); }),
          "cycle: NUL in string key");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::CycleOptions o;
              o.domain = std::string("x\0y", 3);
              dealcode::CycleCodec c("k", o);
          }),
          "cycle: NUL in domain");

    // boundaries that must SUCCEED
    {
        dealcode::CycleOptions o;
        o.alphabet = "01234567";
        o.length = 21; // 8^21 == 2^63 exactly
        dealcode::CycleCodec codec("k", o);
        CHECK(codec.capacity() == (UINT64_C(1) << 63),
              "cycle: capacity == 2^63 accepted");
        CHECK(codec.max_cycle() == 0, "cycle: single cycle");
    }
    {
        dealcode::CycleOptions o;
        o.alphabet = "dec";
        o.length = 2; // 10^2 == 100 exactly
        dealcode::CycleCodec codec("k", o);
        CHECK(codec.capacity() == 100, "cycle: capacity == 100 accepted");
    }
}

// SPEC section 11.3 behaviour: one cycle = a permutation of the full
// fixed-length space; other cycles refill it in a different order.
static void test_cycle_behaviour()
{
    dealcode::CycleOptions o;
    o.alphabet = "dec";
    o.length = 2; // capacity 100
    dealcode::CycleCodec codec("cycle-behaviour-key", o);
    CHECK(codec.capacity() == 100, "cycle perm: capacity");

    std::vector<std::vector<std::string>> cycles;
    for (std::uint64_t e = 0; e < 3; e++) {
        std::vector<std::string> codes;
        for (std::uint64_t v = 0; v < 100; v++) {
            codes.push_back(codec.encode(e * 100 + v));
            CHECK(codec.decode(codes.back(), e) == e * 100 + v,
                  "cycle perm: roundtrip cycle %llu value %llu",
                  static_cast<unsigned long long>(e),
                  static_cast<unsigned long long>(v));
        }
        auto sorted = codes;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end(),
              "cycle perm: cycle %llu issues distinct codes",
              static_cast<unsigned long long>(e));
        cycles.push_back(std::move(codes));
    }
    // same set every cycle, refilled in a different order
    auto sorted0 = cycles[0], sorted1 = cycles[1], sorted2 = cycles[2];
    std::sort(sorted0.begin(), sorted0.end());
    std::sort(sorted1.begin(), sorted1.end());
    std::sort(sorted2.begin(), sorted2.end());
    CHECK(sorted0 == sorted1 && sorted1 == sorted2,
          "cycle perm: every cycle covers the same code set");
    CHECK(cycles[0] != cycles[1] && cycles[1] != cycles[2] &&
              cycles[0] != cycles[2],
          "cycle perm: cycles must differ in order");

    // wrong cycle decodes, but to a different counter (documented
    // ambiguity: cycle is context)
    const std::string code = codec.encode(7);
    CHECK(codec.decode(code, 0) == 7, "cycle perm: right cycle");
    const std::uint64_t other = codec.decode(code, 1);
    CHECK(other != 7 && other >= 100 && other < 200,
          "cycle perm: wrong cycle gives that cycle's counter");

    // counter-space top: 2^63 - 1 roundtrips, 2^63 is out of range
    const std::uint64_t top = (UINT64_C(1) << 63) - 1;
    CHECK(codec.decode(codec.encode(top), top / 100) == top,
          "cycle perm: 2^63 - 1 roundtrip");
    CHECK(throws<dealcode::RangeError>(
              [&] { codec.encode(UINT64_C(1) << 63); }),
          "cycle perm: encode(2^63) throws");

    // octal-21: capacity exactly 2^63, a single cycle
    dealcode::CycleOptions oc;
    oc.alphabet = "01234567";
    oc.length = 21;
    dealcode::CycleCodec octal("cycle-behaviour-key", oc);
    CHECK(octal.decode(octal.encode(top), 0) == top,
          "cycle perm: octal-21 top roundtrip");
    CHECK(throws<dealcode::RangeError>(
              [&] { octal.decode(octal.encode(0), 1); }),
          "cycle perm: octal-21 cycle 1 throws");
}

static void test_cycle_move_semantics()
{
    dealcode::CycleOptions o;
    o.alphabet = "crockford";
    o.length = 6;
    dealcode::CycleCodec a("move-key", o);
    const std::string code = a.encode(42);
    const std::uint64_t cap = a.capacity();

    dealcode::CycleCodec b = std::move(a);
    CHECK(b.encode(42) == code && b.decode(code, 0) == 42,
          "cycle move: moved-to codec works");
    CHECK(b.capacity() == cap, "cycle move: capacity preserved");

    // moved-from: accessors are safe (0/empty), operations throw
    CHECK(a.capacity() == 0 && a.max_cycle() == 0 && a.length() == 0 &&
              a.radix() == 0 && a.alphabet().empty(),
          "cycle move: moved-from accessors are inert");
    CHECK(throws<dealcode::ConfigError>([&] { a.encode(1); }),
          "cycle move: moved-from encode throws");
    CHECK(throws<dealcode::ConfigError>([&] { a.decode(code, 0); }),
          "cycle move: moved-from decode throws");

    dealcode::CycleCodec c("other-key", o);
    c = std::move(b);
    CHECK(c.encode(42) == code, "cycle move: move-assignment works");
}

static void test_cycle_exception_types()
{
    dealcode::CycleOptions o;
    o.alphabet = "dec";
    o.length = 6;
    dealcode::CycleCodec codec("exc-key", o);

    // hierarchy: all cycle errors root at dealcode::Error/std::runtime_error
    CHECK(throws<dealcode::InvalidCodeError>(
              [&] { codec.decode("12345", 0); }),
          "cycle exc: short code -> InvalidCodeError");
    CHECK(throws<dealcode::Error>([&] { codec.decode("12345", 0); }),
          "cycle exc: derives from Error");
    CHECK(throws<std::runtime_error>([&] { codec.decode("12345", 0); }),
          "cycle exc: derives from std::runtime_error");
    CHECK(throws<dealcode::RangeError>(
              [&] { codec.decode("123456", codec.max_cycle() + 1); }),
          "cycle exc: bad cycle -> RangeError");
    CHECK(throws<dealcode::InvalidCodeError>(
              [&] { codec.decode(std::string("12345\0", 6), 0); }),
          "cycle exc: NUL in code -> InvalidCodeError");

    // long-garbage echo is truncated like Codec::decode
    try {
        codec.decode(std::string(300, 'x'), 0);
        CHECK(false, "cycle exc: long garbage must throw");
    } catch (const dealcode::InvalidCodeError &e) {
        const std::string what = e.what();
        CHECK(what.size() < 200 &&
                  what.find("(300 bytes)") != std::string::npos,
              "cycle exc: truncated echo, got \"%s\"", what.c_str());
    }
}

/* Regression tests for the QA round-2 findings: the wrapper must reject
 * embedded U+0000 explicitly instead of silently truncating at the NUL
 * when converting to the C API's NUL-terminated strings. */
template <typename E, typename F>
static void nul_expect_throw(F fn, const char *label)
{
    g_checks++;
    try {
        fn();
        g_failures++;
        std::printf("FAIL: %s: no exception\n", label);
    } catch (const E &) {
        /* expected */
    } catch (...) {
        g_failures++;
        std::printf("FAIL: %s: wrong exception type\n", label);
    }
}

static void test_embedded_nul_rejection()
{
    using namespace dealcode;
    nul_expect_throw<ConfigError>([] { Codec c(std::string("ab\0cd", 5)); },
                                  "NUL in string key");
    nul_expect_throw<ConfigError>(
        [] {
            Options o;
            o.domain = std::string("x\0y", 3);
            Codec c("k", o);
        },
        "NUL in domain");
    nul_expect_throw<ConfigError>(
        [] {
            Options o;
            o.alphabet = std::string("0123456789\0abc", 14);
            Codec c("k", o);
        },
        "NUL in alphabet");
    Codec c("k");
    const std::string code = c.encode(826816);
    nul_expect_throw<InvalidCodeError>(
        [&] { (void)c.decode(code + std::string("\0", 1)); },
        "NUL in decode input");
    CHECK(c.decode(code) == 826816, "clean decode still works");
}

// ------------------------------------------------------------------------
// Integer range mode (SPEC section 12)
// ------------------------------------------------------------------------

static dealcode::RangeCodec make_range_codec(const tv_range_config_t &cfg)
{
    dealcode::RangeOptions opts;
    opts.low = cfg.low;
    opts.high = cfg.high;
    opts.domain = cfg.domain;
    if (cfg.key_hex != nullptr)
        return dealcode::RangeCodec(hex_decode(cfg.key_hex), opts);
    return dealcode::RangeCodec(std::string_view(cfg.key_string), opts);
}

static void test_v1r_vectors()
{
    for (std::size_t i = 0; i < TV_V1R_COUNT; i++) {
        const tv_range_config_t &cfg = TV_V1R[i];
        try {
            dealcode::RangeCodec codec = make_range_codec(cfg);

            // derived domain (SPEC section 12.2): radix, m, capacity must
            // all match the vector file (capacity == radix^m pins m).
            CHECK(codec.radix() == static_cast<int>(cfg.radix),
                  "v1r %s: radix %d, want %u", cfg.name, codec.radix(),
                  cfg.radix);
            CHECK(codec.capacity() == cfg.capacity,
                  "v1r %s: capacity %llu, want %llu", cfg.name,
                  static_cast<unsigned long long>(codec.capacity()),
                  static_cast<unsigned long long>(cfg.capacity));
            {
                std::uint64_t power = 1;
                bool overflow = false;
                for (int j = 0; j < cfg.m; j++) {
                    if (power > UINT64_MAX / cfg.radix) {
                        overflow = true;
                        break;
                    }
                    power *= cfg.radix;
                }
                CHECK(!overflow && power == cfg.capacity,
                      "v1r %s: capacity must equal radix^m", cfg.name);
            }
            CHECK(codec.low() == cfg.low && codec.high() == cfg.high,
                  "v1r %s: low/high accessors", cfg.name);

            for (std::size_t j = 0; j < cfg.n_vectors; j++) {
                const tv_range_pair_t &tv = cfg.vectors[j];
                CHECK(codec.encode(tv.n) == tv.code,
                      "v1r %s: encode(%llu) != %llu", cfg.name,
                      static_cast<unsigned long long>(tv.n),
                      static_cast<unsigned long long>(tv.code));
                CHECK(codec.decode(tv.code) == tv.n,
                      "v1r %s: decode(%llu) != %llu", cfg.name,
                      static_cast<unsigned long long>(tv.code),
                      static_cast<unsigned long long>(tv.n));
            }

            for (std::size_t j = 0; j < cfg.n_invalid_codes; j++) {
                CHECK(throws<dealcode::InvalidCodeError>(
                          [&] { codec.decode(cfg.invalid_codes[j]); }),
                      "v1r %s: decode(%llu) must throw InvalidCodeError",
                      cfg.name,
                      static_cast<unsigned long long>(cfg.invalid_codes[j]));
            }

            for (std::size_t j = 0; j < cfg.n_range_counters; j++) {
                CHECK(throws<dealcode::RangeError>(
                          [&] { codec.encode(cfg.range_counters[j]); }),
                      "v1r %s: encode(%llu) must throw RangeError", cfg.name,
                      static_cast<unsigned long long>(cfg.range_counters[j]));
            }
        } catch (const std::exception &e) {
            CHECK(false, "v1r %s: unexpected exception: %s", cfg.name,
                  e.what());
        }
    }
}

static void test_v1r_invalid_configs()
{
    for (std::size_t i = 0; i < TV_V1R_INVALID_CONFIG_COUNT; i++) {
        const tv_range_invalid_config_t &tv = TV_V1R_INVALID_CONFIGS[i];
        bool threw = false;
        std::string what;
        try {
            dealcode::RangeOptions opts;
            opts.low = tv.low;
            opts.high = tv.high;
            if (tv.domain != nullptr)
                opts.domain = tv.domain;
            if (tv.key_hex != nullptr)
                dealcode::RangeCodec(hex_decode(tv.key_hex), opts);
            else
                dealcode::RangeCodec(std::string_view(tv.key_string), opts);
        } catch (const dealcode::ConfigError &e) {
            threw = true;
            what = e.what();
        }
        CHECK(threw, "v1r invalid-config %s: must throw ConfigError",
              tv.name);
        CHECK(threw && what.size() > std::string("RangeCodec(): ").size(),
              "v1r invalid-config %s: what() must carry a diagnostic",
              tv.name);
    }
}

// Range construction reuses the plain key/domain guards verbatim; check
// the exact diagnostics (prefixed RangeCodec()), plus the bound rules.
static void test_range_config_errors()
{
    try {
        dealcode::RangeCodec codec(std::string_view("crockford"),
                                   { 100000, 999999, "" });
        CHECK(false, "range guard B: \"crockford\" must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "RangeCodec(): string key \"crockford\" is a preset "
                  "alphabet name — did you swap the key and alphabet "
                  "fields?",
              "range guard B: exact message, got \"%s\"", e.what());
    }
    // byte keys spelling a preset name are unaffected
    {
        const char *name = "crockford";
        dealcode::RangeCodec codec(
            reinterpret_cast<const std::uint8_t *>(name), 9,
            { 100000, 999999, "" });
        CHECK(codec.decode(codec.encode(5)) == 5,
              "range guard B: byte key \"crockford\" accepted");
    }

    // bound rules
    try {
        dealcode::RangeCodec codec("k", { 10, 9, "" });
        CHECK(false, "range: low > high must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) == "RangeCodec(): low 10 > high 9",
              "range: low > high message, got \"%s\"", e.what());
    }
    try {
        dealcode::RangeCodec codec("k", { 0, 98, "" });
        CHECK(false, "range: span 99 must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "RangeCodec(): range [0, 98] spans 99 values; FF1 needs "
                  "at least 100",
              "range: span message, got \"%s\"", e.what());
    }
    try {
        dealcode::RangeCodec codec("k", { 0, UINT64_C(1) << 63, "" });
        CHECK(false, "range: high 2^63 must throw");
    } catch (const dealcode::ConfigError &e) {
        CHECK(std::string(e.what()) ==
                  "RangeCodec(): high 9223372036854775808 exceeds 2^63 - 1",
              "range: high bound message, got \"%s\"", e.what());
    }
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::RangeCodec codec("k", {}); // defaults {0, 0}: span 1
          }),
          "range: default-constructed options must throw");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::RangeCodec codec("",
                                         { 100000, 999999, "" });
          }),
          "range: empty string key");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::RangeCodec codec(
                  "k", { 100000, 999999, std::string(256, 'a') });
          }),
          "range: domain > 255 bytes");

    // embedded NUL rejection, as in Codec
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::RangeCodec codec(std::string("ab\0cd", 5),
                                         { 100000, 999999, "" });
          }),
          "range: NUL in string key");
    CHECK(throws<dealcode::ConfigError>([] {
              dealcode::RangeCodec codec(
                  "k", { 100000, 999999, std::string("x\0y", 3) });
          }),
          "range: NUL in domain");

    // boundaries that must SUCCEED
    {
        dealcode::RangeCodec codec("k", { 0, 99, "" }); // span exactly 100
        CHECK(codec.capacity() == 100 && codec.radix() == 10,
              "range: span 100 -> 10^2");
    }
    {
        // full counter space: N = 2^63 = 128^9 exactly
        dealcode::RangeCodec codec("k",
                                   { 0, (UINT64_C(1) << 63) - 1, "" });
        CHECK(codec.capacity() == (UINT64_C(1) << 63) &&
                  codec.radix() == 128,
              "range: N == 2^63 -> 128^9, capacity 2^63");
        const std::uint64_t top = (UINT64_C(1) << 63) - 1;
        CHECK(codec.decode(codec.encode(top)) == top,
              "range: full-space top counter roundtrips");
    }
}

// SPEC section 12.3/12.4 behaviour: bijection when N is an admissible
// power, dead-zone rejection, and low/high/domain binding the permutation.
static void test_range_behaviour()
{
    // N = 121 = 11^2: no dead zone, encode is a bijection onto the range
    {
        dealcode::RangeCodec codec("range-behaviour-key",
                                   { 1000, 1120, "" });
        CHECK(codec.capacity() == 121 && codec.radix() == 11,
              "range perm: capacity 121 = 11^2");
        std::vector<std::uint64_t> codes;
        for (std::uint64_t n = 0; n < 121; n++) {
            codes.push_back(codec.encode(n));
            CHECK(codes.back() >= 1000 && codes.back() <= 1120,
                  "range perm: code in [1000, 1120]");
            CHECK(codec.decode(codes.back()) == n,
                  "range perm: roundtrip n=%llu",
                  static_cast<unsigned long long>(n));
        }
        std::sort(codes.begin(), codes.end());
        bool covers = codes.size() == 121;
        for (std::size_t i = 0; covers && i < codes.size(); i++)
            covers = codes[i] == 1000 + i;
        CHECK(covers, "range perm: the 121 codes cover the whole range");
    }

    // dead zone: [low + capacity, high] never issued, rejected by decode
    {
        dealcode::RangeCodec codec("range-behaviour-key",
                                   { 100000, 999999, "" });
        CHECK(codec.capacity() == 884736, "range dead: capacity 96^3");
        const std::uint64_t top_issued = 100000 + codec.capacity() - 1;
        const std::uint64_t last = codec.encode(codec.capacity() - 1);
        CHECK(last >= 100000 && last <= top_issued &&
                  codec.decode(last) == codec.capacity() - 1,
              "range dead: last counter roundtrips in the issued slice");
        for (std::uint64_t dead : { top_issued + 1, UINT64_C(999999) })
            CHECK(throws<dealcode::InvalidCodeError>(
                      [&] { codec.decode(dead); }),
                  "range dead: decode(%llu) must throw",
                  static_cast<unsigned long long>(dead));
        CHECK(throws<dealcode::InvalidCodeError>(
                  [&] { codec.decode(99999); }),
              "range dead: below low throws");
        CHECK(throws<dealcode::InvalidCodeError>(
                  [&] { codec.decode(1000000); }),
              "range dead: above high throws");
        CHECK(throws<dealcode::RangeError>(
                  [&] { codec.encode(codec.capacity()); }),
              "range dead: encode(capacity) throws");
        CHECK(throws<dealcode::RangeError>(
                  [&] { codec.encode(UINT64_MAX); }),
              "range dead: encode(UINT64_MAX) throws");
    }

    // low, high and domain all bind the permutation (SPEC section 12.3)
    {
        dealcode::RangeCodec a("k", { 100000, 999999, "" });
        dealcode::RangeCodec b("k", { 100000, 999998, "" });
        dealcode::RangeCodec c("k", { 100000, 999999, "x" });
        CHECK(a.capacity() == b.capacity(),
              "range bind: same capacity for a fair comparison");
        bool ab = true, ac = true, bc = true;
        for (std::uint64_t n = 0; n < 32; n++) {
            const std::uint64_t ca = a.encode(n), cb = b.encode(n),
                                cc = c.encode(n);
            ab = ab && ca == cb;
            ac = ac && ca == cc;
            bc = bc && cb == cc;
        }
        CHECK(!ab && !ac && !bc,
              "range bind: low/high/domain must all bind the permutation");
    }
}

static void test_range_move_semantics()
{
    dealcode::RangeCodec a("move-key", { 100000, 999999, "" });
    const std::uint64_t code = a.encode(42);
    const std::uint64_t cap = a.capacity();

    dealcode::RangeCodec b = std::move(a);
    CHECK(b.encode(42) == code && b.decode(code) == 42,
          "range move: moved-to codec works");
    CHECK(b.capacity() == cap && b.low() == 100000 && b.high() == 999999,
          "range move: accessors preserved");

    // moved-from: accessors are safe (0), operations throw
    CHECK(a.capacity() == 0 && a.low() == 0 && a.high() == 0 &&
              a.radix() == 0,
          "range move: moved-from accessors are inert");
    CHECK(throws<dealcode::ConfigError>([&] { a.encode(1); }),
          "range move: moved-from encode throws");
    CHECK(throws<dealcode::ConfigError>([&] { a.decode(code); }),
          "range move: moved-from decode throws");

    dealcode::RangeCodec c("other-key", { 100000, 999999, "" });
    c = std::move(b);
    CHECK(c.encode(42) == code, "range move: move-assignment works");
}

static void test_range_exception_types()
{
    dealcode::RangeCodec codec("exc-key", { 100000, 999999, "" });

    // hierarchy: all range errors root at dealcode::Error/std::runtime_error
    CHECK(throws<dealcode::InvalidCodeError>([&] { codec.decode(0); }),
          "range exc: below low -> InvalidCodeError");
    CHECK(throws<dealcode::Error>([&] { codec.decode(0); }),
          "range exc: derives from Error");
    CHECK(throws<std::runtime_error>([&] { codec.decode(0); }),
          "range exc: derives from std::runtime_error");
    CHECK(throws<dealcode::RangeError>(
              [&] { codec.encode(codec.capacity()); }),
          "range exc: exhausted counter -> RangeError");
    CHECK(throws<dealcode::Error>(
              [&] { codec.encode(codec.capacity()); }),
          "range exc: RangeError derives from Error");
}

int main()

{
    test_nist_ff1();
    test_v1_vectors();
    test_v1_invalid_configs();
    test_exceptions();
    test_preset_name_guards();
    test_error_details();
    test_key_rules();
    test_move_semantics();
    test_accessors_and_defaults();
    test_roundtrips();
    test_counter_bound_rejection();
    test_embedded_nul_rejection();
    test_v1c_vectors();
    test_v1c_invalid_configs();
    test_cycle_config_errors();
    test_cycle_behaviour();
    test_cycle_move_semantics();
    test_cycle_exception_types();
    test_v1r_vectors();
    test_v1r_invalid_configs();
    test_range_config_errors();
    test_range_behaviour();
    test_range_move_semantics();
    test_range_exception_types();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
