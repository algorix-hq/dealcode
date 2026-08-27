// The 9 official NIST FF1-AES sample vectors (encrypt AND decrypt).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import { FF1 } from "../dist/esm/ff1.js";

const vectorsPath = fileURLToPath(
  new URL("../../testvectors/ff1_nist.json", import.meta.url),
);
const { vectors } = JSON.parse(readFileSync(vectorsPath, "utf8"));

// Character index = numeral value in the vector file.
const CHARS = "0123456789abcdefghijklmnopqrstuvwxyz";
const toNumerals = (s) => [...s].map((c) => CHARS.indexOf(c));
const toString = (xs) => xs.map((x) => CHARS[x]).join("");

assert.equal(vectors.length, 9);

for (const v of vectors) {
  test(`NIST FF1 sample ${v.sample} (${v.cipher}, radix ${v.radix})`, () => {
    const ff1 = new FF1(Buffer.from(v.key_hex, "hex"), v.radix);
    const tweak = Buffer.from(v.tweak_hex, "hex");
    const plain = toNumerals(v.plaintext);
    const cipher = toNumerals(v.ciphertext);

    assert.equal(toString(ff1.encrypt(tweak, plain)), v.ciphertext);
    assert.equal(toString(ff1.decrypt(tweak, cipher)), v.plaintext);
  });
}
