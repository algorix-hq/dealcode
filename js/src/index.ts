/**
 * dealcode — collision-free, random-looking codes from a counter.
 *
 * TypeScript/JavaScript implementation of the dealcode spec (format v1),
 * built on FF1 format-preserving encryption (NIST SP 800-38G).
 *
 * ```ts
 * import { Dealcode } from "dealcode";
 *
 * const codec = new Dealcode({ key: process.env.DEALCODE_KEY! });
 * codec.encode(0);          // "c4334d" — 6 hex chars
 * codec.decode("c4334d");   // 0n
 * ```
 *
 * @packageDocumentation
 */

export { Dealcode, type DealcodeOptions } from "./codec.js";
export { ALPHABETS, type AlphabetName } from "./alphabets.js";
export {
  DealcodeError,
  ConfigError,
  CounterRangeError,
  InvalidCodeError,
} from "./errors.js";
