/** Alphabet presets and normalization rules (SPEC.md §3). */

/**
 * The preset alphabets, by name. The character at index `i` represents
 * numeral value `i`; codes are rendered and parsed big-endian.
 */
export const ALPHABETS = {
  /** Decimal digits. No decode normalization. */
  dec: "0123456789",
  /** Lowercase hexadecimal. Decode lowercases ASCII input. */
  hex: "0123456789abcdef",
  /** RFC 4648 base32. Decode uppercases ASCII input. */
  base32: "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
  /** Crockford base32. Decode uppercases, then maps O→0, I→1, L→1. */
  crockford: "0123456789ABCDEFGHJKMNPQRSTVWXYZ",
  /** Lowercase base36. Decode lowercases ASCII input. */
  base36: "0123456789abcdefghijklmnopqrstuvwxyz",
  /** Bitcoin base58. No decode normalization. */
  base58: "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz",
  /** Base62 (digits, uppercase, lowercase). No decode normalization. */
  base62: "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
  /** RFC 4648 §5 base64url. No decode normalization. */
  base64url:
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
} as const;

/** A preset alphabet name. */
export type AlphabetName = keyof typeof ALPHABETS;

/** Resolved alphabet: characters plus the decode-time normalization rule. */
export interface ResolvedAlphabet {
  /** Preset name, or `null` for a custom alphabet. */
  readonly name: AlphabetName | null;
  readonly chars: string;
  readonly normalize: (s: string) => string;
}

function identity(s: string): string {
  return s;
}

/** ASCII-only lowercase: maps A–Z only; everything else untouched. */
function asciiLower(s: string): string {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    out += c >= 0x41 && c <= 0x5a ? String.fromCharCode(c + 32) : s[i];
  }
  return out;
}

/** ASCII-only uppercase: maps a–z only; everything else untouched. */
function asciiUpper(s: string): string {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    out += c >= 0x61 && c <= 0x7a ? String.fromCharCode(c - 32) : s[i];
  }
  return out;
}

function crockfordNormalize(s: string): string {
  let out = "";
  const upper = asciiUpper(s);
  for (const ch of upper) {
    out += ch === "O" ? "0" : ch === "I" || ch === "L" ? "1" : ch;
  }
  return out;
}

/** True when the ASCII-lowercase of `s` is a preset alphabet name. */
export function matchesPresetName(s: string): boolean {
  return Object.prototype.hasOwnProperty.call(ALPHABETS, asciiLower(s));
}

const NORMALIZERS: Record<AlphabetName, (s: string) => string> = {
  dec: identity,
  hex: asciiLower,
  base32: asciiUpper,
  crockford: crockfordNormalize,
  base36: asciiLower,
  base58: identity,
  base62: identity,
  base64url: identity,
};

/**
 * Resolve a preset name or a custom alphabet string (SPEC.md §3.2).
 * Preset names win on conflict. Throws a plain `Error` with a descriptive
 * message on invalid custom alphabets; the codec wraps it in `ConfigError`.
 */
export function resolveAlphabet(alphabet: string): ResolvedAlphabet {
  if (
    typeof alphabet === "string" &&
    Object.prototype.hasOwnProperty.call(ALPHABETS, alphabet)
  ) {
    const name = alphabet as AlphabetName;
    return { name, chars: ALPHABETS[name], normalize: NORMALIZERS[name] };
  }
  if (typeof alphabet !== "string") {
    throw new Error(
      "custom alphabet must be a string of 2-94 characters " +
        "(or one of the preset names: " + Object.keys(ALPHABETS).join(", ") + ")",
    );
  }
  const lowered = asciiLower(alphabet);
  if (Object.prototype.hasOwnProperty.call(ALPHABETS, lowered)) {
    throw new Error(
      `custom alphabet "${alphabet}" matches the preset name "${lowered}" ` +
        `— pass "${lowered}" for the preset, or a genuinely custom alphabet`,
    );
  }
  if (alphabet.length < 2 || alphabet.length > 94) {
    throw new Error(
      "custom alphabet must be a string of 2-94 characters " +
        "(or one of the preset names: " + Object.keys(ALPHABETS).join(", ") + ")",
    );
  }
  if (new Set(alphabet).size !== alphabet.length) {
    throw new Error("custom alphabet characters must be distinct");
  }
  for (let i = 0; i < alphabet.length; i++) {
    const c = alphabet.charCodeAt(i);
    if (c < 0x21 || c > 0x7e) {
      throw new Error(
        "custom alphabet must be printable ASCII (0x21-0x7E, no spaces or control characters)",
      );
    }
  }
  return { name: null, chars: alphabet, normalize: identity };
}
