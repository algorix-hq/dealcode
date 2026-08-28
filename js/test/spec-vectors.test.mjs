// Dealcode format-v1 conformance vectors (testvectors/v1.json): every
// vector encodes and decodes exactly, every invalid code is rejected with
// InvalidCodeError, and every normalization case decodes to its counter.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import {
  ConfigError,
  CounterRangeError,
  Dealcode,
  InvalidCodeError,
} from "../dist/esm/index.js";

const vectorsPath = fileURLToPath(
  new URL("../../testvectors/v1.json", import.meta.url),
);
const { spec, configs, invalid_configs } = JSON.parse(
  readFileSync(vectorsPath, "utf8"),
);

assert.equal(spec, "dealcode/v1");

function buildCodec(config) {
  const key =
    config.key_hex !== undefined
      ? Buffer.from(config.key_hex, "hex")
      : config.key_string;
  return new Dealcode({
    key,
    alphabet: config.alphabet === "custom" ? config.custom_alphabet : config.alphabet,
    minLength: config.min_length,
    maxLength: config.max_length,
    domain: config.domain,
  });
}

for (const config of configs) {
  test(`v1.json config "${config.name}"`, () => {
    const codec = buildCodec(config);

    assert.equal(codec.minLength, config.min_length);
    assert.equal(codec.maxLength, config.max_length);
    assert.equal(codec.domain, config.domain);

    for (const { n, code } of config.vectors ?? []) {
      const counter = BigInt(n);
      assert.equal(codec.encode(counter), code, `encode(${n})`);
      assert.equal(codec.decode(code), counter, `decode(${JSON.stringify(code)})`);
    }

    for (const bad of config.invalid_codes ?? []) {
      assert.throws(
        () => codec.decode(bad),
        InvalidCodeError,
        `decode(${JSON.stringify(bad)}) must throw InvalidCodeError`,
      );
    }

    for (const { input, n } of config.normalize ?? []) {
      assert.equal(
        codec.decode(input),
        BigInt(n),
        `normalized decode(${JSON.stringify(input)})`,
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
  test(`v1.json invalid config "${config.name}"`, () => {
    const key =
      config.key_hex !== undefined
        ? Buffer.from(config.key_hex, "hex")
        : config.key_string;
    assert.throws(
      () =>
        new Dealcode({
          key,
          alphabet: config.custom_alphabet ?? config.alphabet,
          minLength: config.min_length,
          maxLength: config.max_length,
          domain: config.domain,
        }),
      ConfigError,
      `config "${config.name}" must be rejected with ConfigError`,
    );
  });
}
