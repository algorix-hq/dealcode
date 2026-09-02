// Integer range mode (SPEC §12): the full v1r.json conformance file
// (vectors, derived radix/capacity, invalid codes, range counters, invalid
// configs) plus behavioural tests — domain selection, small-range bijection,
// the dead zone, range/domain binding, and the Number.MAX_SAFE_INTEGER seam.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import {
  ConfigError,
  CounterRangeError,
  InvalidCodeError,
  RangeDealcode,
} from "../dist/esm/index.js";
import { selectDomain } from "../dist/esm/range.js";

const vectorsPath = fileURLToPath(
  new URL("../../testvectors/v1r.json", import.meta.url),
);
const { spec, configs, invalid_configs } = JSON.parse(
  readFileSync(vectorsPath, "utf8"),
);

assert.equal(spec, "dealcode/v1r");

function buildCodec(config) {
  const key =
    config.key_hex !== undefined
      ? Buffer.from(config.key_hex, "hex")
      : config.key_string;
  return new RangeDealcode({
    key,
    low: BigInt(config.low),
    high: BigInt(config.high),
    domain: config.domain,
  });
}

for (const config of configs) {
  test(`v1r.json config "${config.name}"`, () => {
    const codec = buildCodec(config);

    assert.equal(codec.low, BigInt(config.low));
    assert.equal(codec.high, BigInt(config.high));
    assert.equal(codec.domain, config.domain);
    assert.equal(codec.radix, config.radix);
    assert.equal(codec.capacity, BigInt(config.capacity));

    for (const { n, code } of config.vectors ?? []) {
      assert.equal(codec.encode(BigInt(n)), BigInt(code), `encode(${n})`);
      assert.equal(codec.decode(BigInt(code)), BigInt(n), `decode(${code})`);
    }

    for (const code of config.invalid_codes ?? []) {
      assert.throws(
        () => codec.decode(BigInt(code)),
        InvalidCodeError,
        `decode(${code}) must throw InvalidCodeError`,
      );
    }

    for (const counter of config.range_counters ?? []) {
      assert.throws(
        () => codec.encode(BigInt(counter)),
        CounterRangeError,
        `encode(${counter}) must throw CounterRangeError`,
      );
    }
  });
}

for (const config of invalid_configs) {
  test(`v1r.json invalid config "${config.name}"`, () => {
    const key =
      config.key_hex !== undefined
        ? Buffer.from(config.key_hex, "hex")
        : config.key_string;
    assert.throws(
      () =>
        new RangeDealcode({
          key,
          low: BigInt(config.low),
          high: BigInt(config.high),
          domain: config.domain,
        }),
      ConfigError,
      `config "${config.name}" must be rejected with ConfigError`,
    );
  });
}

// -- domain selection (SPEC §12.2) --------------------------------------------

test("domain selection: known values", () => {
  assert.deepEqual(selectDomain(100n), [10, 2, 100n]);
  assert.deepEqual(selectDomain(900_000n), [96, 3, 884_736n]);
  assert.deepEqual(selectDomain(1_000_000n), [100, 3, 1_000_000n]); // exact power
  assert.deepEqual(selectDomain(2n ** 63n), [128, 9, 2n ** 63n]); // exact power at the bound
  assert.deepEqual(selectDomain(65_536n), [256, 2, 65_536n]); // tie -> smallest m
});

test("domain selection: capacity bounds", () => {
  // capacity <= N always; > 96% for N >= 10^5 (SPEC §12.2)
  const ns = [
    100n, 101n, 999n, 10_000n, 99_999n, 100_000n, 900_000n, 10_000_003n,
    2n ** 32n, 2n ** 53n + 1n, 2n ** 63n - 1n, 2n ** 63n,
  ];
  for (const n of ns) {
    const [radix, m, cap] = selectDomain(n);
    assert.ok(2 <= radix && radix <= 256, `radix for ${n}`);
    assert.ok(2 <= m && m <= 63, `m for ${n}`);
    assert.equal(BigInt(radix) ** BigInt(m), cap);
    assert.ok(cap <= n, `cap <= N for ${n}`);
    assert.ok(BigInt(radix + 1) ** BigInt(m) > n || radix === 256, `maximal radix for ${n}`);
    if (n >= 100_000n) {
      assert.ok(cap * 100n > n * 96n, `capacity/N > 96% for ${n}`);
    }
  }
});

// -- behaviour ----------------------------------------------------------------

test("small range is a full bijection when N is an admissible power", () => {
  const codec = new RangeDealcode({ key: "k", low: 1_000, high: 1_120 }); // N=121 = 11^2, no dead zone
  assert.equal(codec.capacity, 121n);
  const codes = [];
  for (let n = 0; n < 121; n++) {
    const code = codec.encode(n);
    assert.equal(codec.decode(code), BigInt(n), `roundtrip of ${n}`);
    codes.push(code);
  }
  codes.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  assert.deepEqual(
    codes,
    Array.from({ length: 121 }, (_, i) => 1_000n + BigInt(i)),
  );
});

test("dead zone is rejected, the issued top is accepted", () => {
  const codec = new RangeDealcode({ key: "k", low: 100_000, high: 999_999 });
  assert.equal(codec.capacity, 884_736n); // 96^3
  const topIssued = codec.low + codec.capacity - 1n; // 984735
  const topCode = codec.encode(codec.capacity - 1n);
  assert.ok(topCode >= codec.low && topCode <= topIssued);
  assert.equal(codec.decode(topCode), codec.capacity - 1n);
  for (const dead of [topIssued + 1n, 999_999n]) {
    assert.throws(() => codec.decode(dead), InvalidCodeError, `decode(${dead})`);
  }
});

test("low, high, and domain each bind the permutation", () => {
  const a = new RangeDealcode({ key: "k", low: 100_000, high: 999_999 });
  const b = new RangeDealcode({ key: "k", low: 100_000, high: 999_998 });
  const c = new RangeDealcode({ key: "k", low: 100_000, high: 999_999, domain: "x" });
  const d = new RangeDealcode({ key: "k", low: 100_001n, high: 999_999 });
  const firstEight = (x) =>
    Array.from({ length: 8 }, (_, n) => x.encode(n) - x.low).join(",");
  const outs = new Set([a, b, c, d].map(firstEight));
  assert.equal(outs.size, 4);
});

test("Number.MAX_SAFE_INTEGER seam: bigints beyond it, encodeNumber/decodeNumber guards", () => {
  const KEY = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");
  const codec = new RangeDealcode({ key: KEY, low: 0n, high: 2n ** 63n - 1n });
  assert.equal(codec.capacity, 2n ** 63n); // 128^9, the whole counter space

  // Safe numbers and equal bigints agree; unsafe numbers are rejected as such.
  assert.equal(codec.encode(42), codec.encode(42n));
  assert.equal(codec.encode(Number.MAX_SAFE_INTEGER), codec.encode(2n ** 53n - 1n));
  assert.throws(() => codec.encode(2 ** 53), CounterRangeError); // unsafe number
  assert.throws(() => codec.decode(2 ** 53), InvalidCodeError); // unsafe number
  const big = codec.encode(2n ** 53n); // bigint sails past the seam
  assert.equal(codec.decode(big), 2n ** 53n);

  // Number-returning helpers throw instead of rounding silently.
  let sawGuard = 0;
  for (let n = 0n; n < 8n; n++) {
    const code = codec.encode(n);
    if (code > BigInt(Number.MAX_SAFE_INTEGER)) {
      assert.throws(() => codec.encodeNumber(n), CounterRangeError);
      sawGuard += 1;
    } else {
      assert.equal(BigInt(codec.encodeNumber(n)), code);
    }
  }
  assert.ok(sawGuard > 0, "a 2^63 domain must produce codes past 2^53");
  assert.throws(() => codec.decodeNumber(codec.encode(2n ** 53n)), CounterRangeError);

  // In a small range both helpers just work.
  const small = new RangeDealcode({ key: KEY, low: 100_000, high: 999_999 });
  assert.equal(small.decodeNumber(small.encodeNumber(7)), 7);
});

test("guards: constructor and argument type strictness", () => {
  const KEY = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");

  // Preset-name key guard and key rules (reused from Dealcode).
  assert.throws(() => new RangeDealcode({ key: "crockford", low: 100_000, high: 999_999 }), ConfigError);
  assert.throws(() => new RangeDealcode({ key: "", low: 100_000, high: 999_999 }), ConfigError);
  assert.throws(() => new RangeDealcode({ low: 100_000, high: 999_999 }), ConfigError);
  assert.throws(() => new RangeDealcode({}), ConfigError);

  // Bounds: integers in [0, 2^63 - 1], low <= high, span >= 100.
  assert.throws(() => new RangeDealcode({ key: "k", low: 0.5, high: 999_999 }), ConfigError);
  assert.throws(() => new RangeDealcode({ key: "k", low: 0, high: 2 ** 63 }), ConfigError); // unsafe number
  assert.throws(() => new RangeDealcode({ key: "k", low: "0", high: 999_999 }), ConfigError);
  assert.throws(() => new RangeDealcode({ key: "k", low: true, high: 999_999 }), ConfigError);
  assert.throws(() => new RangeDealcode({ key: "k", low: -1, high: 999_999 }), ConfigError);
  assert.throws(() => new RangeDealcode({ key: "k", low: 10, high: 9n }), ConfigError);
  assert.throws(() => new RangeDealcode({ key: "k", low: 0, high: 2n ** 63n }), ConfigError);
  assert.throws(() => new RangeDealcode({ key: "k", low: 0, high: 98 }), ConfigError); // span 99
  assert.throws(
    () => new RangeDealcode({ key: "k", low: 0, high: 999, domain: "x".repeat(256) }),
    ConfigError,
  );
  assert.throws(
    () => new RangeDealcode({ key: "k", low: 0, high: 999, domain: 42 }),
    ConfigError,
  );

  const codec = new RangeDealcode({ key: KEY, low: 100_000, high: 999_999 });
  assert.equal(codec.domain, ""); // default

  // encode accepts number | bigint; rejects other types and non-integers.
  assert.throws(() => codec.encode(1.5), CounterRangeError);
  assert.throws(() => codec.encode("5"), CounterRangeError);
  assert.throws(() => codec.encode(true), CounterRangeError);
  assert.throws(() => codec.encode(-1), CounterRangeError);
  assert.throws(() => codec.encode(codec.capacity), CounterRangeError);

  // decode: integers only — no string codes in this mode (SPEC §12.3).
  assert.throws(() => codec.decode("500000"), InvalidCodeError);
  assert.throws(() => codec.decode(1.5), InvalidCodeError);
  assert.throws(() => codec.decode(true), InvalidCodeError);
  assert.throws(() => codec.decode(99_999), InvalidCodeError); // below low
  assert.throws(() => codec.decode(1_000_000), InvalidCodeError); // above high

  // Frozen instance.
  assert.ok(Object.isFrozen(codec));
});
