// Regression tests for the QA round-2 findings: absurd lengths must fail in
// O(1) with ConfigError (not a raw V8 RangeError), and string inputs with
// U+0000 or unpaired surrogates must be rejected rather than silently
// re-encoded (SPEC §2, §2.1).
import { test } from "node:test";
import assert from "node:assert/strict";

import { Dealcode, ConfigError } from "../dist/esm/index.js";

const NUL = String.fromCharCode(0);
const LONE_HIGH = String.fromCharCode(0xd800);
const LONE_LOW = String.fromCharCode(0xdfff);

test("absurd lengths are rejected in O(1) with ConfigError", () => {
  const t0 = Date.now();
  assert.throws(() => new Dealcode({ key: "k", maxLength: 2 ** 31 }), ConfigError);
  assert.throws(() => new Dealcode({ key: "k", maxLength: 1e9 }), ConfigError);
  assert.throws(() => new Dealcode({ key: "k", minLength: 1e9 }), ConfigError);
  assert.ok(Date.now() - t0 < 100, "must fail fast");
});

test("U+0000 and unpaired surrogates are rejected in string inputs", () => {
  assert.throws(() => new Dealcode({ key: "k", domain: `a${NUL}b` }), ConfigError);
  assert.throws(() => new Dealcode({ key: `a${NUL}b` }), ConfigError);
  assert.throws(() => new Dealcode({ key: "k", domain: LONE_HIGH }), ConfigError);
  assert.throws(() => new Dealcode({ key: "k", domain: LONE_LOW }), ConfigError);
  assert.throws(() => new Dealcode({ key: LONE_HIGH }), ConfigError);
  // legitimate Unicode still works
  const codec = new Dealcode({ key: "k", domain: "한국어-✅-😀" });
  assert.equal(codec.decode(codec.encode(42)), 42n);
});
