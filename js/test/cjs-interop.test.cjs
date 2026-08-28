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

test("errors thrown by the CJS build satisfy instanceof of the ESM classes", async () => {
  const esm = await import("../dist/esm/index.js");
  const key = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");
  const codec = new cjs.Dealcode({ key });

  const catchFrom = (fn) => {
    try {
      fn();
    } catch (err) {
      return err;
    }
    assert.fail("expected throw");
  };

  const configErr = catchFrom(() => new cjs.Dealcode({ key: "" }));
  const rangeErr = catchFrom(() => codec.encode(-1));
  const codeErr = catchFrom(() => codec.decode("!!!!!!"));

  // same-realm (CJS) instanceof still works
  assert.ok(configErr instanceof cjs.ConfigError);
  assert.ok(configErr instanceof cjs.DealcodeError);

  // cross-realm: the CJS-thrown errors are recognized by the ESM classes
  assert.ok(configErr instanceof esm.ConfigError);
  assert.ok(configErr instanceof esm.DealcodeError);
  assert.ok(rangeErr instanceof esm.CounterRangeError);
  assert.ok(rangeErr instanceof esm.DealcodeError);
  assert.ok(codeErr instanceof esm.InvalidCodeError);
  assert.ok(codeErr instanceof esm.DealcodeError);

  // ...and not confused with sibling classes
  assert.ok(!(configErr instanceof esm.CounterRangeError));
  assert.ok(!(rangeErr instanceof esm.InvalidCodeError));
  assert.ok(!(codeErr instanceof esm.ConfigError));

  // unrelated errors don't match
  assert.ok(!(new Error("nope") instanceof esm.DealcodeError));
});
