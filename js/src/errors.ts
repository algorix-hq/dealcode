/**
 * Error classes (SPEC.md §8).
 *
 * All dealcode errors extend {@link DealcodeError}, so a single
 * `catch (e) { if (e instanceof DealcodeError) ... }` covers everything
 * the library can throw.
 */

/** Base class for every error thrown by dealcode. */
export class DealcodeError extends Error {
  constructor(message: string) {
    super(message);
    this.name = new.target.name;
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
