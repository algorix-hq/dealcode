// Behavioural tests: roundtrips across stage boundaries, config validation,
// key-material flexibility, number/bigint handling, decodeNumber overflow,
// and counter-space handling when radix^maxLength > 2^63.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";

import {
  ALPHABETS,
  ConfigError,
  CounterRangeError,
  Dealcode,
  DealcodeError,
  InvalidCodeError,
} from "../dist/esm/index.js";

const KEY = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");

test("roundtrip: a few thousand counters incl. stage boundaries, no collisions", () => {
  // hex, minLength 2 (16^2 = 256 >= 100), maxLength 5 -> stages at 256, 4096, 65536.
  const codec = new Dealcode({ key: KEY, alphabet: "hex", minLength: 2, maxLength: 5 });
  assert.equal(codec.capacity, 16n ** 5n);

  const counters = new Set();
  for (let n = 0; n < 3000; n++) counters.add(n);
  for (const boundary of [256, 4096, 65536]) {
    for (let delta = -3; delta <= 3; delta++) counters.add(boundary + delta);
  }
  counters.add(Number(codec.capacity) - 1);

  const seen = new Set();
  for (const n of counters) {
    const code = codec.encode(n);
    // stage/length invariant: d = max(minLength, digit count of n)
    const expectedLength = Math.max(2, n === 0 ? 1 : n.toString(16).length);
    assert.equal(code.length, expectedLength, `length of encode(${n})`);
    assert.ok(!seen.has(code), `collision at ${n}`);
    seen.add(code);
    assert.equal(codec.decode(code), BigInt(n), `roundtrip of ${n}`);
    assert.equal(codec.decodeNumber(code), n);
  }
});

test("default maxLength per spec examples", () => {
  assert.equal(new Dealcode({ key: KEY }).maxLength, 15); // hex
  assert.equal(new Dealcode({ key: KEY, alphabet: "dec" }).maxLength, 18);
  assert.equal(new Dealcode({ key: KEY, alphabet: "base32" }).maxLength, 12);
  assert.equal(new Dealcode({ key: KEY, alphabet: "crockford" }).maxLength, 12);
  assert.equal(new Dealcode({ key: KEY, alphabet: "base36" }).maxLength, 12);
  assert.equal(new Dealcode({ key: KEY, alphabet: "base58", minLength: 5 }).maxLength, 10);
  assert.equal(new Dealcode({ key: KEY, alphabet: "base62", minLength: 4 }).maxLength, 10);
  assert.equal(new Dealcode({ key: KEY, alphabet: "base64url", minLength: 4 }).maxLength, 10);
});

test("readonly props and ALPHABETS export", () => {
  const codec = new Dealcode({ key: KEY, alphabet: "crockford", domain: "coupons" });
  assert.equal(codec.alphabet, "0123456789ABCDEFGHJKMNPQRSTVWXYZ");
  assert.equal(codec.alphabet, ALPHABETS.crockford);
  assert.equal(codec.radix, 32);
  assert.equal(codec.minLength, 6);
  assert.equal(codec.domain, "coupons");
  assert.equal(typeof codec.capacity, "bigint");
  assert.equal(Object.keys(ALPHABETS).length, 8);
  assert.equal(ALPHABETS.hex, "0123456789abcdef");
});

test("config errors", () => {
  // every ConfigError is also a DealcodeError
  assert.throws(() => new Dealcode({ key: "" }), ConfigError);
  assert.throws(() => new Dealcode({ key: "" }), DealcodeError);
  assert.throws(() => new Dealcode({ key: new Uint8Array(0) }), ConfigError);
  assert.throws(() => new Dealcode({ key: 123 }), ConfigError);
  assert.throws(() => new Dealcode({}), ConfigError);

  // alphabets
  assert.throws(() => new Dealcode({ key: KEY, alphabet: "x" }), ConfigError); // too short
  assert.throws(() => new Dealcode({ key: KEY, alphabet: "aab" }), ConfigError); // duplicate
  assert.throws(() => new Dealcode({ key: KEY, alphabet: "ab cd" }), ConfigError); // space
  assert.throws(() => new Dealcode({ key: KEY, alphabet: "abé" }), ConfigError); // non-ASCII
  assert.throws(() => new Dealcode({ key: KEY, alphabet: "a".repeat(95) }), ConfigError);

  // lengths
  assert.throws(() => new Dealcode({ key: KEY, minLength: 1 }), ConfigError);
  assert.throws(() => new Dealcode({ key: KEY, minLength: 6.5 }), ConfigError);
  // radix^minLength < 100: binary alphabet, 2^6 = 64
  assert.throws(() => new Dealcode({ key: KEY, alphabet: "01", minLength: 6 }), ConfigError);
  assert.ok(new Dealcode({ key: KEY, alphabet: "01", minLength: 7 })); // 128 >= 100
  assert.throws(() => new Dealcode({ key: KEY, minLength: 8, maxLength: 7 }), ConfigError);
  assert.throws(() => new Dealcode({ key: KEY, maxLength: 12.5 }), ConfigError);
  // radix^maxLength > 2^128: 62^22 > 2^128
  assert.throws(
    () => new Dealcode({ key: KEY, alphabet: "base62", maxLength: 22 }),
    ConfigError,
  );
  assert.ok(new Dealcode({ key: KEY, alphabet: "base62", maxLength: 21 })); // 62^21 < 2^128

  // domain
  assert.throws(() => new Dealcode({ key: KEY, domain: 42 }), ConfigError);
  assert.throws(() => new Dealcode({ key: KEY, domain: "x".repeat(256) }), ConfigError);
  assert.throws(() => new Dealcode({ key: KEY, domain: "é".repeat(128) }), ConfigError); // 256 UTF-8 bytes
  assert.ok(new Dealcode({ key: KEY, domain: "x".repeat(255) }));
});

test("error names are set properly", () => {
  try {
    new Dealcode({ key: "" });
    assert.fail("expected throw");
  } catch (err) {
    assert.equal(err.name, "ConfigError");
    assert.ok(err instanceof Error);
  }
  const codec = new Dealcode({ key: KEY });
  try {
    codec.encode(-1);
    assert.fail("expected throw");
  } catch (err) {
    assert.equal(err.name, "CounterRangeError");
  }
  try {
    codec.decode("!!!!!!");
    assert.fail("expected throw");
  } catch (err) {
    assert.equal(err.name, "InvalidCodeError");
  }
});

test("key material flexibility", () => {
  // A string key and its UTF-8 bytes derive the same AES key -> same codes.
  const fromString = new Dealcode({ key: "correct horse battery staple" });
  const fromUtf8Bytes = new Dealcode({
    key: Buffer.from("correct horse battery staple", "utf8"),
  });
  // Both are derived: a 28-byte key is not 16/24/32.
  assert.equal(fromString.encode(42), fromUtf8Bytes.encode(42));

  // 32 raw bytes are used directly; the same bytes as a hex STRING are
  // derived instead (strings are never auto-decoded) -> different codes.
  const raw = createHash("sha256").update("seed").digest();
  const direct = new Dealcode({ key: raw });
  const hexString = new Dealcode({ key: raw.toString("hex") });
  assert.notEqual(direct.encode(42), hexString.encode(42));

  // Plain Uint8Array (not Buffer) works the same as Buffer.
  const asUint8 = new Dealcode({ key: new Uint8Array(raw) });
  assert.equal(direct.encode(42), asUint8.encode(42));

  // Non-16/24/32-byte keys are derived, and 16/24/32-byte keys of different
  // sizes all construct fine.
  for (const size of [1, 15, 16, 17, 20, 24, 32, 33, 64]) {
    const codec = new Dealcode({ key: Buffer.alloc(size, 7) });
    assert.equal(codec.decode(codec.encode(99)), 99n);
  }

  // Derivation rule: AES-256 key = SHA-256("dealcode/v1/kdf" || material).
  const derivedKey = createHash("sha256")
    .update(Buffer.concat([Buffer.from("dealcode/v1/kdf"), Buffer.from("pw", "utf8")]))
    .digest();
  assert.equal(
    new Dealcode({ key: "pw" }).encode(7),
    new Dealcode({ key: derivedKey }).encode(7),
  );
});

test("number and bigint inputs", () => {
  const codec = new Dealcode({ key: KEY });
  assert.equal(codec.encode(5), codec.encode(5n));
  assert.equal(codec.encode(0), codec.encode(0n));
  assert.throws(() => codec.encode(1.5), CounterRangeError);
  assert.throws(() => codec.encode(NaN), CounterRangeError);
  assert.throws(() => codec.encode(Infinity), CounterRangeError);
  assert.throws(() => codec.encode(2 ** 53), CounterRangeError); // unsafe number
  assert.throws(() => codec.encode("42"), CounterRangeError);
  assert.throws(() => codec.encode(-1), CounterRangeError);
  assert.throws(() => codec.encode(-1n), CounterRangeError);
  // hex default: capacity = 16^15 < 2^63
  assert.equal(codec.capacity, 16n ** 15n);
  assert.equal(codec.encode(codec.capacity - 1n).length, 15);
  assert.throws(() => codec.encode(codec.capacity), CounterRangeError);
  // bigints beyond MAX_SAFE_INTEGER work
  const big = 2n ** 55n;
  assert.equal(codec.decode(codec.encode(big)), big);
});

test("decode input validation", () => {
  const codec = new Dealcode({ key: KEY }); // hex 6..15
  assert.throws(() => codec.decode(42), InvalidCodeError); // not a string
  assert.throws(() => codec.decode("abc"), InvalidCodeError); // too short
  assert.throws(() => codec.decode("0".repeat(16)), InvalidCodeError); // too long
  assert.throws(() => codec.decode("00000g"), InvalidCodeError); // bad char
  assert.throws(() => codec.decode(""), InvalidCodeError);
  // preset normalization: hex decode lowercases
  const code = codec.encode(123456);
  assert.equal(codec.decode(code.toUpperCase()), 123456n);
});

test("custom alphabets have no normalization", () => {
  const codec = new Dealcode({ key: KEY, alphabet: "abcdefghij", minLength: 2 });
  const code = codec.encode(7);
  assert.equal(codec.decode(code), 7n);
  assert.throws(() => codec.decode(code.toUpperCase()), InvalidCodeError);
});

test("domains produce unrelated codes", () => {
  const a = new Dealcode({ key: KEY, domain: "orders" });
  const b = new Dealcode({ key: KEY, domain: "coupons" });
  const c = new Dealcode({ key: KEY });
  assert.notEqual(a.encode(42), b.encode(42));
  assert.notEqual(a.encode(42), c.encode(42));
  assert.equal(a.decode(a.encode(42)), 42n);
});

test("decodeNumber overflow", () => {
  // dec maxLength 18: counters up to 10^18 - 1 > 2^53
  const codec = new Dealcode({ key: KEY, alphabet: "dec" });
  const small = codec.encode(123);
  assert.equal(codec.decodeNumber(small), 123);
  assert.equal(typeof codec.decodeNumber(small), "number");

  const big = BigInt(Number.MAX_SAFE_INTEGER) + 1n;
  const bigCode = codec.encode(big);
  assert.equal(codec.decode(bigCode), big); // decode (bigint) is fine
  assert.throws(() => codec.decodeNumber(bigCode), CounterRangeError);

  const edge = codec.encode(BigInt(Number.MAX_SAFE_INTEGER));
  assert.equal(codec.decodeNumber(edge), Number.MAX_SAFE_INTEGER);
});

test("capacity clamps at 2^63 when radix^maxLength exceeds it (hex 16)", () => {
  // 16^16 = 2^64 > 2^63 -> capacity is exactly 2^63.
  const codec = new Dealcode({ key: KEY, minLength: 16, maxLength: 16 });
  assert.equal(codec.capacity, 2n ** 63n);

  // Counters right at the boundary.
  const top = 2n ** 63n - 1n;
  const topCode = codec.encode(top);
  assert.equal(topCode.length, 16);
  assert.equal(codec.decode(topCode), top);
  assert.throws(() => codec.encode(2n ** 63n), CounterRangeError);

  // Half the 16-char hex code space decrypts to counters >= 2^63; those
  // codes were never issued and must be rejected by decode. Scan a few
  // candidate codes: (pseudo)randomly ~half accept, ~half reject.
  let accepted = 0;
  let rejected = 0;
  for (let i = 0; i < 64; i++) {
    const candidate = i.toString(16).padStart(16, "0");
    try {
      const n = codec.decode(candidate);
      assert.ok(n >= 0n && n < 2n ** 63n);
      accepted += 1;
    } catch (err) {
      assert.ok(err instanceof InvalidCodeError);
      rejected += 1;
    }
  }
  assert.ok(accepted > 0, "expected some candidates inside the counter space");
  assert.ok(rejected > 0, "expected some candidates outside the counter space");
});

test("fixed-length codes (minLength === maxLength)", () => {
  const codec = new Dealcode({ key: KEY, minLength: 8, maxLength: 8 });
  for (const n of [0n, 1n, 16n ** 8n - 1n]) {
    const code = codec.encode(n);
    assert.equal(code.length, 8);
    assert.equal(codec.decode(code), n);
  }
  assert.throws(() => codec.encode(16n ** 8n), CounterRangeError);
});

test("codec instances are immutable", () => {
  const codec = new Dealcode({ key: KEY });
  assert.ok(Object.isFrozen(codec));
  assert.throws(() => {
    "use strict";
    codec.minLength = 99;
  }, TypeError);
});
