// Fixed-length cycling mode (SPEC §11): the full v1c.json conformance file
// (vectors, capacity/max_cycle, invalid codes, normalization, range counters,
// invalid cycles, invalid configs) plus behavioural tests — full-cycle
// permutation, cross-cycle ambiguity, and the final-partial-cycle boundary.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import {
  ConfigError,
  CounterRangeError,
  CyclingDealcode,
  InvalidCodeError,
} from "../dist/esm/index.js";

const vectorsPath = fileURLToPath(
  new URL("../../testvectors/v1c.json", import.meta.url),
);
const { spec, configs, invalid_configs } = JSON.parse(
  readFileSync(vectorsPath, "utf8"),
);

assert.equal(spec, "dealcode/v1c");

function buildCodec(config) {
  const key =
    config.key_hex !== undefined
      ? Buffer.from(config.key_hex, "hex")
      : config.key_string;
  return new CyclingDealcode({
    key,
    alphabet: config.alphabet === "custom" ? config.custom_alphabet : config.alphabet,
    length: config.length,
    domain: config.domain,
  });
}

for (const config of configs) {
  test(`v1c.json config "${config.name}"`, () => {
    const codec = buildCodec(config);

    assert.equal(codec.length, config.length);
    assert.equal(codec.domain, config.domain);
    assert.equal(codec.capacity, BigInt(config.capacity));
    assert.equal(codec.maxCycle, BigInt(config.max_cycle));

    for (const { n, code } of config.vectors ?? []) {
      const counter = BigInt(n);
      const cycle = counter / codec.capacity;
      assert.equal(codec.encode(counter), code, `encode(${n})`);
      assert.equal(codec.cycleOf(counter), cycle, `cycleOf(${n})`);
      assert.equal(
        codec.decode(code, cycle),
        counter,
        `decode(${JSON.stringify(code)}, ${cycle})`,
      );
    }

    for (const { cycle, code } of config.invalid_codes ?? []) {
      assert.throws(
        () => codec.decode(code, BigInt(cycle)),
        InvalidCodeError,
        `decode(${JSON.stringify(code)}, ${cycle}) must throw InvalidCodeError`,
      );
    }

    for (const { cycle, input, n } of config.normalize ?? []) {
      assert.equal(
        codec.decode(input, BigInt(cycle)),
        BigInt(n),
        `normalized decode(${JSON.stringify(input)}, ${cycle})`,
      );
    }

    for (const counter of config.range_counters ?? []) {
      assert.throws(
        () => codec.encode(BigInt(counter)),
        CounterRangeError,
        `encode(${counter}) must throw CounterRangeError`,
      );
    }

    const probe = codec.encode(0);
    for (const cycle of config.invalid_cycles ?? []) {
      assert.throws(
        () => codec.decode(probe, BigInt(cycle)),
        CounterRangeError,
        `decode(probe, ${cycle}) must throw CounterRangeError`,
      );
    }
  });
}

for (const config of invalid_configs) {
  test(`v1c.json invalid config "${config.name}"`, () => {
    const key =
      config.key_hex !== undefined
        ? Buffer.from(config.key_hex, "hex")
        : config.key_string;
    assert.throws(
      () =>
        new CyclingDealcode({
          key,
          alphabet: config.custom_alphabet ?? config.alphabet,
          length: config.length,
          domain: config.domain,
        }),
      ConfigError,
      `config "${config.name}" must be rejected with ConfigError`,
    );
  });
}

// -- behaviour ----------------------------------------------------------------

test("full cycle is a permutation, and cycles refill the space in different orders", () => {
  const codec = new CyclingDealcode({ key: "k", alphabet: "dec", length: 2 }); // capacity 100
  assert.equal(codec.capacity, 100n);

  const cycles = [];
  for (let e = 0; e < 3; e++) {
    const codes = [];
    for (let v = 0; v < 100; v++) {
      const n = e * 100 + v;
      const code = codec.encode(n);
      assert.equal(code.length, 2, `encode(${n}) length`);
      assert.equal(codec.decode(code, e), BigInt(n), `roundtrip of ${n}`);
      codes.push(code);
    }
    assert.equal(new Set(codes).size, 100, `cycle ${e} is a permutation`);
    cycles.push(codes);
  }
  // Same 100-code space every cycle, refilled in a different order.
  const asSet = (codes) => new Set(codes);
  assert.deepEqual(asSet(cycles[0]), asSet(cycles[1]));
  assert.deepEqual(asSet(cycles[1]), asSet(cycles[2]));
  assert.notDeepEqual(cycles[0], cycles[1]);
  assert.notDeepEqual(cycles[1], cycles[2]);
  assert.notDeepEqual(cycles[0], cycles[2]);
});

test("decoding under the wrong cycle yields a different counter, not an error", () => {
  const codec = new CyclingDealcode({ key: "k", alphabet: "crockford", length: 6 });
  const code = codec.encode(7);
  assert.equal(codec.decode(code, 0), 7n);
  assert.notEqual(codec.decode(code, 1), 7n); // documented ambiguity: cycle is context
  assert.notEqual(codec.decode(code, 1n), 7n); // bigint cycle accepted too
});

test("final partial cycle: 2^63 − 1 round-trips, 2^63 is rejected", () => {
  const codec = new CyclingDealcode({ key: "k", alphabet: "dec", length: 2 });
  const top = 2n ** 63n - 1n;
  const code = codec.encode(top);
  assert.equal(codec.decode(code, codec.cycleOf(top)), top);
  assert.equal(codec.cycleOf(top), codec.maxCycle);
  assert.throws(() => codec.encode(2n ** 63n), CounterRangeError);
  assert.throws(() => codec.encode(-1), CounterRangeError);
  // In the final partial cycle, codes past the end of the counter space decode
  // fine in earlier cycles but are invalid in the last one: find one by
  // scanning the last cycle for a value the space cannot reach.
  const lastBase = codec.maxCycle * codec.capacity;
  let valid = 0;
  for (let v = 0n; v < codec.capacity; v++) {
    // Encode via cycle 0 to get a well-formed code, then check it against the
    // last cycle: it must either decode in-range or throw InvalidCodeError.
    const probe = codec.encode(v);
    try {
      const n = codec.decode(probe, codec.maxCycle);
      assert.ok(n >= lastBase && n < 2n ** 63n);
      valid += 1;
    } catch (err) {
      assert.ok(err instanceof InvalidCodeError);
    }
  }
  // Exactly 2^63 − lastBase counters exist in the final partial cycle.
  assert.equal(BigInt(valid), 2n ** 63n - lastBase);
});

test("guards: constructor and argument validation", () => {
  const KEY = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");

  // length must be an integer in [2, 128], checked before any power (fast).
  const t0 = Date.now();
  assert.throws(() => new CyclingDealcode({ key: "k", length: 1e9 }), ConfigError);
  assert.throws(() => new CyclingDealcode({ key: "k", length: 2 ** 31 }), ConfigError);
  assert.ok(Date.now() - t0 < 100, "must fail fast");
  assert.throws(() => new CyclingDealcode({ key: "k", length: 1 }), ConfigError);
  assert.throws(() => new CyclingDealcode({ key: "k", length: 129 }), ConfigError);
  assert.throws(() => new CyclingDealcode({ key: "k", length: 6.5 }), ConfigError);

  // radix^length bounds: >= 100 and <= 2^63 (exactly 2^63 is legal).
  assert.throws(
    () => new CyclingDealcode({ key: "k", alphabet: "abcdefghi", length: 2 }), // 9^2 = 81
    ConfigError,
  );
  assert.throws(
    () => new CyclingDealcode({ key: "k", alphabet: "hex", length: 16 }),
    ConfigError,
  );
  const octal21 = new CyclingDealcode({ key: KEY, alphabet: "01234567", length: 21 });
  assert.equal(octal21.capacity, 2n ** 63n);
  assert.equal(octal21.maxCycle, 0n);

  // Preset-name key guard and lookalike alphabet guard (reused from Dealcode).
  assert.throws(() => new CyclingDealcode({ key: "crockford" }), ConfigError);
  assert.throws(() => new CyclingDealcode({ key: "k", alphabet: "HEX" }), ConfigError);
  assert.throws(() => new CyclingDealcode({ key: "" }), ConfigError);
  assert.throws(() => new CyclingDealcode({}), ConfigError);
  assert.throws(() => new CyclingDealcode({ key: "k", domain: "x".repeat(256) }), ConfigError);

  const codec = new CyclingDealcode({ key: KEY });
  assert.equal(codec.length, 6); // default
  assert.equal(codec.alphabet, "0123456789abcdef");
  assert.equal(codec.capacity, 16n ** 6n);

  // encode accepts number | bigint; rejects unsafe numbers and other types.
  assert.equal(codec.encode(42), codec.encode(42n));
  assert.throws(() => codec.encode(2 ** 60), CounterRangeError); // unsafe number
  assert.throws(() => codec.encode(1.5), CounterRangeError);
  assert.throws(() => codec.encode("7"), CounterRangeError);
  assert.throws(() => codec.cycleOf(-1), CounterRangeError);
  assert.throws(() => codec.cycleOf(2n ** 63n), CounterRangeError);

  // decode: cycle validation, fixed-length gate before normalization.
  const code = codec.encode(0);
  assert.throws(() => codec.decode(code, -1), CounterRangeError);
  assert.throws(() => codec.decode(code, codec.maxCycle + 1n), CounterRangeError);
  assert.throws(() => codec.decode(code, 0.5), CounterRangeError);
  assert.throws(() => codec.decode(code, "0"), CounterRangeError);
  assert.throws(() => codec.decode(42, 0), InvalidCodeError);
  assert.throws(() => codec.decode(code + "0", 0), InvalidCodeError);
  assert.throws(() => codec.decode(code.slice(1), 0), InvalidCodeError);
  assert.throws(() => codec.decode("zzzzzz", 0), InvalidCodeError); // not in hex alphabet
  assert.throws(() => codec.decode("0".repeat(10_000_000), 0), InvalidCodeError);

  // Frozen instance.
  assert.ok(Object.isFrozen(codec));
});
