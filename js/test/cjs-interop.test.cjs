// The CommonJS build must be loadable via require() and agree with the ESM
// build on outputs.
"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");

const cjs = require("../dist/cjs/index.js");

test("CJS build exports the public API", () => {
  assert.equal(typeof cjs.Dealcode, "function");
  assert.equal(typeof cjs.ALPHABETS, "object");
  assert.equal(typeof cjs.DealcodeError, "function");
  assert.equal(typeof cjs.ConfigError, "function");
  assert.equal(typeof cjs.CounterRangeError, "function");
  assert.equal(typeof cjs.InvalidCodeError, "function");
});

test("CJS build agrees with the ESM build", async () => {
  const esm = await import("../dist/esm/index.js");
  const key = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");
  const a = new cjs.Dealcode({ key, domain: "interop" });
  const b = new esm.Dealcode({ key, domain: "interop" });
  for (let n = 0; n < 50; n++) {
    const code = a.encode(n);
    assert.equal(code, b.encode(n));
    assert.equal(b.decode(code), BigInt(n));
  }
});
