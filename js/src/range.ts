/** Integer range mode (SPEC.md §12, tweak namespace `dealcode/v1r/`). */

import { assertCleanUnicode, COUNTER_BOUND, resolveKey } from "./codec.js";
import { ConfigError, CounterRangeError, InvalidCodeError } from "./errors.js";
import { FF1 } from "./ff1.js";

const RANGE_TWEAK_PREFIX = "dealcode/v1r/";
/** Numerals stay one byte in every FF1 core (SPEC §12.2). */
const MAX_RADIX = 256n;

/** Largest integer r with r^m <= n (exact integer arithmetic). */
function iroot(n: bigint, m: bigint): bigint {
  if (n < 1n) return 0n;
  let lo = 1n;
  let hi = 1n;
  while (hi ** m <= n) hi *= 2n;
  while (hi - lo > 1n) {
    const mid = (lo + hi) / 2n;
    if (mid ** m <= n) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/**
 * SPEC §12.2: `[radix, m, capacity]` — largest `radix^m <= n`, smallest `m`
 * on ties.
 * @internal Exported for the test suite; not public API.
 */
export function selectDomain(n: bigint): [number, number, bigint] {
  let bestCapacity = 0n;
  let bestRadix = 0n;
  let bestM = 0n;
  for (let m = 2n; m <= 63n; m++) {
    let r = iroot(n, m);
    if (r > MAX_RADIX) r = MAX_RADIX;
    if (r < 2n) continue;
    const c = r ** m;
    if (c > bestCapacity) {
      // Strict '>' keeps the smallest m on ties.
      bestCapacity = c;
      bestRadix = r;
      bestM = m;
    }
  }
  return [Number(bestRadix), Number(bestM), bestCapacity];
}

/** Validate a number|bigint range bound and widen it to bigint (ConfigError). */
function toBound(value: number | bigint, name: string): bigint {
  if (typeof value === "bigint") return value;
  if (typeof value === "number" && Number.isSafeInteger(value)) {
    return BigInt(value);
  }
  if (typeof value === "number") {
    throw new ConfigError(
      `${name} must be a safe integer; got ${value} (use a bigint for large values)`,
    );
  }
  throw new ConfigError(`${name} must be a number or bigint, got ${typeof value}`);
}

/** Options for {@link RangeDealcode}. */
export interface RangeDealcodeOptions {
  /**
   * Key material (required). Same rules as {@link DealcodeOptions.key}:
   * 16/24/32 raw bytes are used directly as the AES key; any other non-empty
   * bytes or string are derived via `SHA-256("dealcode/v1/kdf" ‖ material)`.
   */
  key: string | Uint8Array;
  /**
   * Smallest integer in the range (required). Must satisfy
   * `0 <= low <= high <= 2^63 − 1`. A `number` must be a safe integer (use
   * `bigint` beyond `Number.MAX_SAFE_INTEGER`).
   */
  low: number | bigint;
  /**
   * Largest integer in the range (required). Same rules as `low`; the range
   * must span at least 100 values (`high − low + 1 >= 100`, FF1 structural
   * minimum).
   */
  high: number | bigint;
  /**
   * Namespace label bound into the FF1 tweak (default `""`). At most 255
   * UTF-8 bytes.
   */
  domain?: string;
}

/**
 * Integer codes drawn without repetition from `[low, high]`
 * (dealcode format v1r, see SPEC.md §12).
 *
 * Counters `0 <= n < capacity` map bijectively to integer codes in
 * `[low, low + capacity − 1]` through a single FF1 call — no loops, no
 * cycle-walking. `capacity` is the largest FF1 domain (`radix^m` with
 * `radix <= 256`) that fits in the range, so it can be slightly smaller
 * than `high − low + 1`; the uncovered top slice is never issued and is
 * rejected by {@link decode}.
 *
 * Built for ranges like 100000–999999: every code is a 6-digit integer
 * with no leading zero, safe to store in an integer column.
 *
 * ```ts
 * import { RangeDealcode } from "dealcode";
 *
 * const codec = new RangeDealcode({ key, low: 100_000, high: 999_999 });
 * codec.capacity;               // 884736n — 96^3, 98.3% of the range
 * const code = codec.encode(0); // a bigint in [100000n, 984735n]
 * codec.decode(code);           // 0n
 * ```
 *
 * Instances are immutable and safe for concurrent use.
 */
export class RangeDealcode {
  /** Smallest integer in the range. */
  readonly low: bigint;
  /** Largest integer in the range. */
  readonly high: bigint;
  /** Namespace label bound into the FF1 tweak. */
  readonly domain: string;
  /** Internal FF1 radix (SPEC §12.2); informational. */
  readonly radix: number;
  /** Number of issuable codes: the largest `radix^m <= high − low + 1`. */
  readonly capacity: bigint;

  readonly #m: number;
  readonly #ff1: FF1;
  readonly #tweak: Buffer;
  readonly #radixBig: bigint;

  constructor(options: RangeDealcodeOptions) {
    if (options === null || typeof options !== "object") {
      throw new ConfigError("RangeDealcode requires an options object with a key");
    }
    const { key, low, high, domain = "" } = options;
    if (key === undefined) {
      throw new ConfigError("key is required");
    }
    const aesKey = resolveKey(key);
    const lowBig = toBound(low, "low");
    const highBig = toBound(high, "high");
    if (lowBig < 0n || lowBig > highBig || highBig > COUNTER_BOUND - 1n) {
      throw new ConfigError("low/high must satisfy 0 <= low <= high <= 2^63 - 1");
    }
    if (highBig - lowBig + 1n < 100n) {
      throw new ConfigError("range must span at least 100 values (FF1 minimum domain)");
    }
    if (typeof domain !== "string") {
      throw new ConfigError("domain must be a string");
    }
    assertCleanUnicode(domain, "domain");
    if (Buffer.byteLength(domain, "utf8") > 255) {
      throw new ConfigError("domain must be at most 255 UTF-8 bytes");
    }

    const [radix, m, capacity] = selectDomain(highBig - lowBig + 1n);
    this.low = lowBig;
    this.high = highBig;
    this.domain = domain;
    this.radix = radix;
    this.capacity = capacity;
    this.#m = m;
    this.#ff1 = new FF1(aesKey, radix);
    // BigInt.toString(10) renders with no leading zeros and "0" for zero,
    // exactly the decimal(·) the spec requires (SPEC §12.3).
    this.#tweak = Buffer.from(
      `${RANGE_TWEAK_PREFIX}${lowBig}/${highBig}/${domain}`,
      "utf8",
    );
    this.#radixBig = BigInt(radix);
    Object.freeze(this);
  }

  /**
   * Map counter `n` to its integer code in `[low, low + capacity)`.
   *
   * @param n - A non-negative integer in `[0, capacity)`. A `number` must be
   *   a safe integer (use `bigint` beyond `Number.MAX_SAFE_INTEGER`).
   * @returns The code as a `bigint` (codes can exceed
   *   `Number.MAX_SAFE_INTEGER`; see {@link encodeNumber}).
   * @throws {@link CounterRangeError} if `n` is not an integer or is out of
   *   range.
   */
  encode(n: number | bigint): bigint {
    const counter = this.#toCounter(n);
    if (counter < 0n || counter >= this.capacity) {
      throw new CounterRangeError(`counter ${counter} out of range [0, ${this.capacity})`);
    }
    const cipher = this.#ff1.encrypt(this.#tweak, this.#toNumerals(counter));
    let v = 0n;
    for (const x of cipher) v = v * this.#radixBig + BigInt(x);
    return this.low + v;
  }

  /**
   * Like {@link encode}, but returns a `number` for convenience.
   *
   * @throws {@link CounterRangeError} if the code exceeds
   *   `Number.MAX_SAFE_INTEGER` (use {@link encode} in that case), or if `n`
   *   is invalid (see {@link encode}).
   */
  encodeNumber(n: number | bigint): number {
    const code = this.encode(n);
    if (code > BigInt(Number.MAX_SAFE_INTEGER)) {
      throw new CounterRangeError(
        `code ${code} exceeds Number.MAX_SAFE_INTEGER; use encode() instead`,
      );
    }
    return Number(code);
  }

  /**
   * Map integer `code` back to its counter.
   *
   * @param code - An integer in `[low, low + capacity)`. A `number` must be
   *   a safe integer (use `bigint` beyond `Number.MAX_SAFE_INTEGER`). Codes
   *   are integers end to end in this mode — strings are rejected.
   * @returns The counter as a `bigint` (counters can exceed
   *   `Number.MAX_SAFE_INTEGER`; see {@link decodeNumber}).
   * @throws {@link InvalidCodeError} if `code` is not an integer, is outside
   *   `[low, high]`, or falls in the unissued dead zone
   *   `[low + capacity, high]`.
   */
  decode(code: number | bigint): bigint {
    const c = this.#toCode(code);
    if (c < this.low || c > this.high) {
      throw new InvalidCodeError(`code ${c} outside range [${this.low}, ${this.high}]`);
    }
    const v = c - this.low;
    if (v >= this.capacity) {
      throw new InvalidCodeError(
        `code ${c} in the unissued top slice of the range (capacity ${this.capacity})`,
      );
    }
    const plain = this.#ff1.decrypt(this.#tweak, this.#toNumerals(v));
    let n = 0n;
    for (const x of plain) n = n * this.#radixBig + BigInt(x);
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
  decodeNumber(code: number | bigint): number {
    const n = this.decode(code);
    if (n > BigInt(Number.MAX_SAFE_INTEGER)) {
      throw new CounterRangeError(
        `decoded counter ${n} exceeds Number.MAX_SAFE_INTEGER; use decode() instead`,
      );
    }
    return Number(n);
  }

  // -- helpers ----------------------------------------------------------------

  /** Validate a number|bigint counter input and widen it to bigint. */
  #toCounter(value: number | bigint): bigint {
    if (typeof value === "bigint") return value;
    if (typeof value === "number" && Number.isSafeInteger(value)) {
      return BigInt(value);
    }
    if (typeof value === "number") {
      throw new CounterRangeError(
        `counter must be a safe integer; got ${value} (use a bigint for large values)`,
      );
    }
    throw new CounterRangeError(`counter must be a number or bigint, got ${typeof value}`);
  }

  /** Validate a number|bigint code input and widen it to bigint. */
  #toCode(value: number | bigint): bigint {
    if (typeof value === "bigint") return value;
    if (typeof value === "number" && Number.isSafeInteger(value)) {
      return BigInt(value);
    }
    if (typeof value === "number") {
      throw new InvalidCodeError(
        `code must be a safe integer; got ${value} (use a bigint for large values)`,
      );
    }
    throw new InvalidCodeError(`code must be a number or bigint, got ${typeof value}`);
  }

  /** STR: represent value as exactly `m` base-radix numerals, big-endian. */
  #toNumerals(value: bigint): number[] {
    const out = new Array<number>(this.#m);
    const r = this.#radixBig;
    let v = value;
    for (let i = this.#m - 1; i >= 0; i--) {
      out[i] = Number(v % r);
      v /= r;
    }
    return out;
  }
}
