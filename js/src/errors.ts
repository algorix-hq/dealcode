/**
 * Error classes (SPEC.md §8).
 *
 * All dealcode errors extend {@link DealcodeError}, so a single
 * `catch (e) { if (e instanceof DealcodeError) ... }` covers everything
 * the library can throw.
 *
 * Instances are branded with a well-known symbol so `instanceof` also works
 * across module realms (e.g. an error thrown by the CJS build tested against
 * the ESM classes, or two copies of the package in one dependency tree).
 */

/** Cross-realm brand: present (non-enumerable) on every dealcode error. */
const BRAND = Symbol.for("dealcode.error");

/** Base class for every error thrown by dealcode. */
export class DealcodeError extends Error {
  constructor(message: string) {
    super(message);
    this.name = new.target.name;
    Object.defineProperty(this, BRAND, { value: true });
  }

  /**
   * Realm-independent `instanceof`: any branded dealcode error matches the
   * base class; a subclass additionally requires a matching `name`. Real
   * (same-realm) instances are branded in the constructor, so they still
   * pass; the prototype chain itself is untouched.
   */
  static override [Symbol.hasInstance](value: unknown): boolean {
    if (typeof value !== "object" || value === null) return false;
    // Same-realm instances (and user subclasses): ordinary prototype check.
    if (this.prototype.isPrototypeOf(value)) return true;
    // Cross-realm instances: brand plus (for subclasses) the error name.
    if ((value as Record<symbol, unknown>)[BRAND] !== true) return false;
    return this.name === "DealcodeError" || (value as Error).name === this.name;
  }
}

/**
 * Invalid codec configuration: bad key material, alphabet, lengths, or
 * domain. Thrown at construction time only.
 */
export class ConfigError extends DealcodeError {}

/**
 * `encode` was called with a counter outside `[0, capacity)`, or with a
 * non-integer / unsafe `number`. (Named `CounterRangeError` so it does not
 * shadow the global `RangeError`.)
 */
export class CounterRangeError extends DealcodeError {}

/**
 * `decode` input failed validation: wrong length, character not in the
 * alphabet, or a well-formed string this codec could never have issued.
 */
export class InvalidCodeError extends DealcodeError {}
