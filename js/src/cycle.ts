/** Fixed-length cycling mode (SPEC.md §11, tweak namespace `dealcode/v1c/`). */

import { resolveAlphabet, type ResolvedAlphabet } from "./alphabets.js";
import { assertCleanUnicode, COUNTER_BOUND, resolveKey } from "./codec.js";
import { ConfigError, CounterRangeError, InvalidCodeError } from "./errors.js";
import { FF1 } from "./ff1.js";

const CYCLE_TWEAK_PREFIX = "dealcode/v1c/";

/** Options for {@link CyclingDealcode}. */
export interface CyclingDealcodeOptions {
  /**
   * Key material (required). Same rules as {@link DealcodeOptions.key}:
   * 16/24/32 raw bytes are used directly as the AES key; any other non-empty
   * bytes or string are derived via `SHA-256("dealcode/v1/kdf" ‖ material)`.
   */
  key: string | Uint8Array;
  /**
   * A preset name (`"dec"`, `"hex"`, `"base32"`, `"crockford"`, `"base36"`,
   * `"base58"`, `"base62"`, `"base64url"`) or a custom alphabet string of
   * 2–94 distinct printable ASCII characters. Default: `"hex"`.
   */
  alphabet?: string;
  /**
   * The fixed code length (default 6). Every code is exactly this many
   * characters. Must satisfy `2 <= length <= 128`, `radix^length >= 100`
   * (FF1 structural minimum), and `radix^length <= 2^63` — a cycle must be
   * completable within the counter space (use {@link Dealcode} with
   * `minLength === maxLength` for larger fixed spaces).
   */
  length?: number;
  /**
   * Namespace label bound into the FF1 tweak (default `""`). At most 255
   * UTF-8 bytes.
   */
  domain?: string;
}

/**
 * Fixed-length codes that refill the same space cycle after cycle
 * (dealcode format v1c, see SPEC.md §11).
 *
 * Codes are always exactly `length` characters. Counters `n` map to cycle
 * `n / capacity` and in-cycle value `n % capacity`; every cycle is a
 * different permutation of the same code space (a different FF1 tweak), so
 * when the space is exhausted it refills in a new order instead of growing.
 *
 * Codes REPEAT across cycles by design — keep at most one cycle's codes
 * live per uniqueness scope (`UNIQUE(cycle, code)`, not `UNIQUE(code)`),
 * and store which cycle a live code belongs to: {@link decode} needs it.
 *
 * ```ts
 * import { CyclingDealcode } from "dealcode";
 *
 * const codec = new CyclingDealcode({ key, alphabet: "crockford", length: 6 });
 * const n = 3n * codec.capacity + 7n;      // cycle 3, value 7
 * codec.decode(codec.encode(n), 3);        // n
 * ```
 *
 * Instances are immutable and safe for concurrent use.
 */
export class CyclingDealcode {
  /** The alphabet characters, in numeral order. */
  readonly alphabet: string;
  /** Number of characters in the alphabet. */
  readonly radix: number;
  /** The fixed code length. */
  readonly length: number;
  /** Namespace label bound into the FF1 tweak. */
  readonly domain: string;
  /** Codes per cycle: `radix^length`. */
  readonly capacity: bigint;
  /** Largest usable cycle: `(2^63 − 1) / capacity` (integer division). */
  readonly maxCycle: bigint;

  readonly #alpha: ResolvedAlphabet;
  readonly #ff1: FF1;
  readonly #charIndex: Map<string, number>;
  readonly #radixBig: bigint;

  constructor(options: CyclingDealcodeOptions) {
    if (options === null || typeof options !== "object") {
      throw new ConfigError("CyclingDealcode requires an options object with a key");
    }
    const { key, alphabet = "hex", length = 6, domain = "" } = options;
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

    // Bound the length BEFORE computing any BigInt power (SPEC §11.1).
    if (!Number.isInteger(length) || length < 2 || length > 128) {
      throw new ConfigError("length must be an integer in [2, 128]");
    }
    const capacity = radixBig ** BigInt(length);
    if (capacity < 100n) {
      throw new ConfigError(
        `radix^length must be at least 100 (FF1 minimum domain); ` +
          `got ${radix}^${length}`,
      );
    }
    if (capacity > COUNTER_BOUND) {
      // Exactly 2^63 is legal: that single cycle uses the whole counter space.
      throw new ConfigError(
        `radix^length must not exceed 2^63 in cycling mode — a cycle must ` +
          `be completable; use Dealcode with minLength === maxLength for ` +
          `larger fixed spaces (got ${radix}^${length})`,
      );
    }
    if (typeof domain !== "string") {
      throw new ConfigError("domain must be a string");
    }
    assertCleanUnicode(domain, "domain");
    if (Buffer.byteLength(domain, "utf8") > 255) {
      throw new ConfigError("domain must be at most 255 UTF-8 bytes");
    }

    this.alphabet = alpha.chars;
    this.radix = radix;
    this.length = length;
    this.domain = domain;
    this.capacity = capacity;
    this.maxCycle = (COUNTER_BOUND - 1n) / capacity;
    this.#alpha = alpha;
    this.#ff1 = new FF1(aesKey, radix);
    this.#charIndex = new Map();
    for (let i = 0; i < alpha.chars.length; i++) {
      this.#charIndex.set(alpha.chars[i]!, i);
    }
    this.#radixBig = radixBig;
    Object.freeze(this);
  }

  /**
   * Map counter `n` to its fixed-length code (cycle = `n / capacity`).
   *
   * @param n - A non-negative integer in `[0, 2^63)`. A `number` must be a
   *   safe integer (use `bigint` beyond `Number.MAX_SAFE_INTEGER`).
   * @returns The code string — always exactly `length` characters.
   * @throws {@link CounterRangeError} if `n` is not an integer or is out of
   *   range.
   */
  encode(n: number | bigint): string {
    const counter = this.#toCounter(n, "counter");
    if (counter < 0n || counter >= COUNTER_BOUND) {
      throw new CounterRangeError(`counter ${counter} out of range [0, ${COUNTER_BOUND})`);
    }
    const cycle = counter / this.capacity;
    const v = counter % this.capacity;
    const numerals = this.#toNumerals(v);
    const cipher = this.#ff1.encrypt(this.#tweakFor(cycle), numerals);
    let code = "";
    for (const x of cipher) code += this.alphabet[x];
    return code;
  }

  /**
   * Map a code issued in `cycle` back to its counter.
   *
   * The cycle is required: the same string recurs in every cycle, mapping to
   * a different counter each time — the application must have stored which
   * cycle the code belongs to.
   *
   * Preset-alphabet normalization (e.g. `O→0` for `crockford`) is applied to
   * the input first; custom alphabets must match exactly.
   *
   * @returns The counter `cycle * capacity + v` as a `bigint`.
   * @throws {@link CounterRangeError} if `cycle` is not an integer in
   *   `[0, maxCycle]`.
   * @throws {@link InvalidCodeError} if the code has the wrong length,
   *   contains characters outside the alphabet, or (in the final partial
   *   cycle only) maps past the end of the counter space.
   */
  decode(code: string, cycle: number | bigint): bigint {
    const e = this.#toCounter(cycle, "cycle");
    if (e < 0n || e > this.maxCycle) {
      throw new CounterRangeError(`cycle ${e} out of range [0, ${this.maxCycle}]`);
    }
    if (typeof code !== "string") {
      throw new InvalidCodeError(`code must be a string, got ${typeof code}`);
    }
    // Length gate before normalization (SPEC §7 via §11.2): normalization is
    // length-preserving, and gating first keeps rejection of oversized
    // garbage O(1). Count code points when that is cheap so the message
    // matches the other implementations; valid codes are ASCII, where the
    // counts agree.
    const gate = code.length <= 4 * this.length ? [...code].length : code.length;
    if (gate !== this.length) {
      throw new InvalidCodeError(
        `code length ${gate} != ${this.length} (fixed-length mode)`,
      );
    }
    const normalized = this.#alpha.normalize(code);
    const numerals = new Array<number>(this.length);
    for (let i = 0; i < this.length; i++) {
      const numeral = this.#charIndex.get(normalized[i]!);
      if (numeral === undefined) {
        throw new InvalidCodeError(
          `character ${JSON.stringify(normalized[i])} not in alphabet`,
        );
      }
      numerals[i] = numeral;
    }
    const plain = this.#ff1.decrypt(this.#tweakFor(e), numerals);
    let v = 0n;
    for (const x of plain) v = v * this.#radixBig + BigInt(x);
    const n = e * this.capacity + v;
    if (n >= COUNTER_BOUND) {
      // Only reachable in the final partial cycle.
      throw new InvalidCodeError("code was not issued in this cycle");
    }
    return n;
  }

  /**
   * The cycle that counter `n` belongs to (`n / capacity`).
   *
   * @throws {@link CounterRangeError} if `n` is not an integer in `[0, 2^63)`.
   */
  cycleOf(n: number | bigint): bigint {
    const counter = this.#toCounter(n, "counter");
    if (counter < 0n || counter >= COUNTER_BOUND) {
      throw new CounterRangeError(`counter ${counter} out of range [0, ${COUNTER_BOUND})`);
    }
    return counter / this.capacity;
  }

  // -- helpers ----------------------------------------------------------------

  /** Validate a number|bigint integer input and widen it to bigint. */
  #toCounter(value: number | bigint, what: string): bigint {
    if (typeof value === "bigint") return value;
    if (typeof value === "number" && Number.isSafeInteger(value)) {
      return BigInt(value);
    }
    if (typeof value === "number") {
      throw new CounterRangeError(
        `${what} must be a safe integer; got ${value} (use a bigint for large values)`,
      );
    }
    throw new CounterRangeError(`${what} must be a number or bigint, got ${typeof value}`);
  }

  /** Tweak for cycle e: UTF-8 of "dealcode/v1c/" + decimal(e) + "/" + domain. */
  #tweakFor(cycle: bigint): Buffer {
    // BigInt.toString(10) renders with no leading zeros and "0" for zero,
    // exactly the decimal(e) the spec requires.
    return Buffer.from(`${CYCLE_TWEAK_PREFIX}${cycle}/${this.domain}`, "utf8");
  }

  /** STR: represent value as exactly `length` base-radix numerals, big-endian. */
  #toNumerals(value: bigint): number[] {
    const out = new Array<number>(this.length);
    const r = this.#radixBig;
    let v = value;
    for (let i = this.length - 1; i >= 0; i--) {
      out[i] = Number(v % r);
      v /= r;
    }
    return out;
  }
}
