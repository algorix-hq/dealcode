/** The dealcode codec (SPEC.md format version 1). */

import { createHash } from "node:crypto";

import {
  matchesPresetName,
  resolveAlphabet,
  type ResolvedAlphabet,
} from "./alphabets.js";
import { ConfigError, CounterRangeError, InvalidCodeError } from "./errors.js";
import { FF1 } from "./ff1.js";

/** Counters live in `[0, min(radix^maxLength, 2^63))`. */
const COUNTER_BOUND = 2n ** 63n;
/** `radix^maxLength` must not exceed this. */
const CODESPACE_BOUND = 2n ** 128n;
const TWEAK_PREFIX = "dealcode/v1/";
const KDF_PREFIX = Buffer.from("dealcode/v1/kdf", "ascii");

/** Options for {@link Dealcode}. */
export interface DealcodeOptions {
  /**
   * Key material (required). A `Uint8Array` (or `Buffer`) of exactly 16, 24,
   * or 32 bytes is used directly as the AES key; any other non-empty bytes,
   * or any non-empty string (its UTF-8 bytes), are deterministically derived
   * into an AES-256 key via `SHA-256("dealcode/v1/kdf" ‖ material)`.
   * A hex-looking string is *not* auto-decoded. Generate a good key with
   * `openssl rand -hex 32`.
   */
  key: string | Uint8Array;
  /**
   * A preset name (`"dec"`, `"hex"`, `"base32"`, `"crockford"`, `"base36"`,
   * `"base58"`, `"base62"`, `"base64url"`) or a custom alphabet string of
   * 2–94 distinct printable ASCII characters. Default: `"hex"`.
   */
  alphabet?: string;
  /**
   * Codes start at this length (default 6). Must satisfy `minLength >= 2`
   * and `radix^minLength >= 100`.
   */
  minLength?: number;
  /**
   * Codes grow one character at a time up to this length. Default: the
   * largest `L` with `radix^L <= 2^63 − 1` (hex → 15, dec → 18, base62 → 10,
   * …). Must satisfy `minLength <= maxLength` and `radix^maxLength <= 2^128`.
   */
  maxLength?: number;
  /**
   * Namespace label bound into the FF1 tweak (default `""`). Two codecs with
   * the same key but different domains produce unrelated codes. At most 255
   * UTF-8 bytes.
   */
  domain?: string;
}

/**
 * Rejects U+0000 and unpaired surrogates in string inputs (SPEC.md §2.1) —
 * silently re-encoding them (Buffer would emit U+FFFD) makes the "same"
 * input produce different permutations across languages.
 */
function assertCleanUnicode(value: string, what: string): void {
  if (value.includes("\u0000")) {
    throw new ConfigError(`${what} must not contain U+0000`);
  }
  // String.prototype.isWellFormed needs lib >= ES2024; feature-detect via a
  // cast so the ES2022 build target still compiles, with a regex fallback.
  const isWellFormed = (value as { isWellFormed?: () => boolean }).isWellFormed;
  const wellFormed =
    typeof isWellFormed === "function"
      ? isWellFormed.call(value)
      : !/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?:^|[^\uD800-\uDBFF])[\uDC00-\uDFFF]/.test(value);
  if (!wellFormed) {
    throw new ConfigError(`${what} must be valid Unicode (no unpaired surrogates)`);
  }
}

/** Key material handling (SPEC.md §2.1). */
function resolveKey(key: string | Uint8Array): Buffer {
  let material: Buffer;
  let direct: boolean;
  if (typeof key === "string") {
    if (matchesPresetName(key)) {
      throw new ConfigError(
        `string key "${key}" is a preset alphabet name ` +
          `— did you swap the key and alphabet arguments?`,
      );
    }
    assertCleanUnicode(key, "string key material");
    material = Buffer.from(key, "utf8");
    direct = false; // strings are always derived, never auto-decoded
  } else if (key instanceof Uint8Array) {
    material = Buffer.from(key);
    direct = material.length === 16 || material.length === 24 || material.length === 32;
  } else {
    throw new ConfigError("key must be a string or a Uint8Array");
  }
  if (material.length === 0) {
    throw new ConfigError("key must not be empty");
  }
  if (direct) return material;
  return createHash("sha256").update(KDF_PREFIX).update(material).digest();
}

/** Largest L >= minLength with radix^L <= 2^63 − 1 (SPEC.md §2). */
function defaultMaxLength(radix: bigint, minLength: number): number {
  let length = minLength;
  let cap = radix ** BigInt(minLength);
  while (cap * radix < COUNTER_BOUND) {
    cap *= radix;
    length += 1;
  }
  return length;
}

/**
 * Bijective counter ↔ code mapping (dealcode format v1, see SPEC.md).
 *
 * Maps a non-negative integer counter to a short, random-looking,
 * fixed-alphabet code and back, using FF1 format-preserving encryption
 * (NIST SP 800-38G). The mapping is a keyed permutation: distinct counters
 * always produce distinct codes.
 *
 * ```ts
 * import { Dealcode } from "dealcode";
 *
 * const codec = new Dealcode({ key: process.env.DEALCODE_KEY! });
 * const code = codec.encode(42);   // e.g. "4b71b7"
 * codec.decode(code);              // 42n
 * ```
 *
 * Instances are immutable and safe for concurrent use; create one per code
 * namespace at startup and reuse it. The whole configuration (key, alphabet,
 * lengths, domain) must never change once codes have been issued.
 */
export class Dealcode {
  /** The alphabet characters, in numeral order. */
  readonly alphabet: string;
  /** Number of characters in the alphabet. */
  readonly radix: number;
  /** Length of the shortest codes. */
  readonly minLength: number;
  /** Length of the longest codes. */
  readonly maxLength: number;
  /** Namespace label bound into the FF1 tweak. */
  readonly domain: string;
  /** Number of encodable counters: `min(radix^maxLength, 2^63)`. */
  readonly capacity: bigint;

  readonly #alpha: ResolvedAlphabet;
  readonly #tweak: Buffer;
  readonly #ff1: FF1;
  readonly #charIndex: Map<string, number>;
  /** powers[d] = radix^d for d in [0, maxLength]. */
  readonly #powers: readonly bigint[];
  readonly #radixBig: bigint;

  constructor(options: DealcodeOptions) {
    if (options === null || typeof options !== "object") {
      throw new ConfigError("Dealcode requires an options object with a key");
    }
    const { key, alphabet = "hex", minLength = 6, maxLength, domain = "" } = options;
    if (key === undefined) {
      throw new ConfigError("key is required");
    }
    const aesKey = resolveKey(key);
    let alpha: ResolvedAlphabet;
    try {
      alpha = resolveAlphabet(alphabet);
    } catch (err) {
      throw new ConfigError((err as Error).message);
    }
    const radix = alpha.chars.length;
    const radixBig = BigInt(radix);

    // Bound lengths BEFORE computing any BigInt power (SPEC §2): 128 is the
    // structural maximum, and an unguarded exponent on absurd lengths would
    // either burn CPU or leak a raw V8 "Maximum BigInt size exceeded" error.
    if (!Number.isInteger(minLength) || minLength < 2 || minLength > 128) {
      throw new ConfigError("minLength must be an integer in [2, 128]");
    }
    if (radixBig ** BigInt(minLength) < 100n) {
      throw new ConfigError(
        `radix^minLength must be at least 100 (FF1 minimum domain); ` +
          `got ${radix}^${minLength}`,
      );
    }
    const max = maxLength ?? defaultMaxLength(radixBig, minLength);
    if (!Number.isInteger(max) || max < minLength || max > 128) {
      throw new ConfigError("maxLength must be an integer in [minLength, 128]");
    }
    if (radixBig ** BigInt(max) > CODESPACE_BOUND) {
      throw new ConfigError(
        `radix^maxLength must not exceed 2^128; got ${radix}^${max}`,
      );
    }
    if (typeof domain !== "string") {
      throw new ConfigError("domain must be a string");
    }
    assertCleanUnicode(domain, "domain");
    const tweak = Buffer.from(TWEAK_PREFIX + domain, "utf8");
    if (tweak.length - TWEAK_PREFIX.length > 255) {
      throw new ConfigError("domain must be at most 255 UTF-8 bytes");
    }

    this.alphabet = alpha.chars;
    this.radix = radix;
    this.minLength = minLength;
    this.maxLength = max;
    this.domain = domain;
    this.#alpha = alpha;
    this.#tweak = tweak;
    this.#ff1 = new FF1(aesKey, radix);
    this.#charIndex = new Map();
    for (let i = 0; i < alpha.chars.length; i++) {
      this.#charIndex.set(alpha.chars[i]!, i);
    }
    const powers: bigint[] = [1n];
    for (let d = 1; d <= max; d++) powers.push(powers[d - 1]! * radixBig);
    this.#powers = powers;
    this.#radixBig = radixBig;
    const codespace = powers[max]!;
    this.capacity = codespace < COUNTER_BOUND ? codespace : COUNTER_BOUND;
    Object.freeze(this);
  }

  /**
   * Map counter `n` to its code. O(1) in the counter value.
   *
   * @param n - A non-negative integer in `[0, capacity)`. A `number` must be
   *   a safe integer (use `bigint` beyond `Number.MAX_SAFE_INTEGER`).
   * @returns The code string; its length is between `minLength` and
   *   `maxLength`, growing only when the previous length is exhausted.
   * @throws {@link CounterRangeError} if `n` is not an integer or is out of
   *   range.
   */
  encode(n: number | bigint): string {
    let counter: bigint;
    if (typeof n === "bigint") {
      counter = n;
    } else if (typeof n === "number" && Number.isSafeInteger(n)) {
      counter = BigInt(n);
    } else if (typeof n === "number") {
      throw new CounterRangeError(
        `counter must be a safe integer; got ${n} (use a bigint for large counters)`,
      );
    } else {
      throw new CounterRangeError(`counter must be a number or bigint, got ${typeof n}`);
    }
    if (counter < 0n || counter >= this.capacity) {
      throw new CounterRangeError(`counter ${counter} out of range [0, ${this.capacity})`);
    }
    const [d, base] = this.#stageOf(counter);
    const numerals = this.#toNumerals(counter - base, d);
    const cipher = this.#ff1.encrypt(this.#tweak, numerals);
    let code = "";
    for (const x of cipher) code += this.alphabet[x];
    return code;
  }

  /**
   * Map a code back to its counter.
   *
   * Preset-alphabet normalization (e.g. case folding for `hex`, `O→0` for
   * `crockford`) is applied to the input first; custom alphabets must match
   * exactly.
   *
   * @returns The counter as a `bigint` (counters can exceed
   *   `Number.MAX_SAFE_INTEGER`; see {@link decodeNumber}).
   * @throws {@link InvalidCodeError} if the code has the wrong length,
   *   contains characters outside the alphabet, or was never issued by this
   *   codec. Decode success only proves consistency with the key — the
   *   application still decides whether counter `n` actually exists.
   */
  decode(code: string): bigint {
    if (typeof code !== "string") {
      throw new InvalidCodeError(`code must be a string, got ${typeof code}`);
    }
    // Length gate before normalization: normalization is length-preserving,
    // so this is behaviour-identical, and it keeps rejection of oversized
    // garbage O(1) instead of normalizing megabytes first (SPEC §7). The
    // gate counts code points when that is cheap so the message matches the
    // other implementations; valid codes are ASCII, where the counts agree.
    const gate =
      code.length <= 4 * this.maxLength ? [...code].length : code.length;
    if (gate < this.minLength || gate > this.maxLength) {
      throw new InvalidCodeError(
        `code length ${gate} outside [${this.minLength}, ${this.maxLength}]`,
      );
    }
    const normalized = this.#alpha.normalize(code);
    const d = normalized.length;
    const numerals = new Array<number>(d);
    for (let i = 0; i < d; i++) {
      const numeral = this.#charIndex.get(normalized[i]!);
      if (numeral === undefined) {
        throw new InvalidCodeError(
          `character ${JSON.stringify(normalized[i])} not in alphabet`,
        );
      }
      numerals[i] = numeral;
    }
    const plain = this.#ff1.decrypt(this.#tweak, numerals);
    let v = 0n;
    for (const x of plain) v = v * this.#radixBig + BigInt(x);
    const base = d === this.minLength ? 0n : this.#powers[d - 1]!;
    if (d > this.minLength && v >= this.#powers[d]! - base) {
      throw new InvalidCodeError("code was not issued by this codec");
    }
    const n = base + v;
    if (n >= COUNTER_BOUND) {
      throw new InvalidCodeError("code was not issued by this codec");
    }
    return n;
  }

  /**
   * Like {@link decode}, but returns a `number` for convenience.
   *
   * @throws {@link CounterRangeError} if the decoded counter exceeds
   *   `Number.MAX_SAFE_INTEGER` (use {@link decode} in that case).
   * @throws {@link InvalidCodeError} if the code is invalid (see
   *   {@link decode}).
   */
  decodeNumber(code: string): number {
    const n = this.decode(code);
    if (n > BigInt(Number.MAX_SAFE_INTEGER)) {
      throw new CounterRangeError(
        `decoded counter ${n} exceeds Number.MAX_SAFE_INTEGER; use decode() instead`,
      );
    }
    return Number(n);
  }

  // -- helpers ----------------------------------------------------------------

  /** Stage of counter n: [code length d, stage base]. */
  #stageOf(n: bigint): [number, bigint] {
    const powers = this.#powers;
    const m = this.minLength;
    if (n < powers[m]!) return [m, 0n];
    let d = m + 1;
    while (n >= powers[d]!) d += 1;
    return [d, powers[d - 1]!];
  }

  /** STR: represent value as exactly m base-radix numerals, big-endian. */
  #toNumerals(value: bigint, m: number): number[] {
    const out = new Array<number>(m);
    const r = this.#radixBig;
    let v = value;
    for (let i = m - 1; i >= 0; i--) {
      out[i] = Number(v % r);
      v /= r;
    }
    return out;
  }
}
