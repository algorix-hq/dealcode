# TypeScript / JavaScript

TypeScript implementation of the [spec](../spec.md). Requires Node.js ≥ 18.
Zero runtime dependencies — AES and SHA-256 come from `node:crypto`. Ships
ESM and CommonJS builds with full TypeScript types.

Source of truth: [`js/` on GitHub](https://github.com/algorix-hq/dealcode/tree/main/js)
· [full README](https://github.com/algorix-hq/dealcode/blob/main/js/README.md)

## Install

```sh
npm install dealcode
```


## Minimal example

```ts
import { Dealcode } from "dealcode";

const codec = new Dealcode({ key: process.env.DEALCODE_KEY! });

codec.encode(0);        // e.g. '767a5b' (6 hex chars; depends on your key)
const code = codec.encode(1);    // never collides with any other counter
codec.decode(code);              // 1n  (bigint — counters can exceed 2^53)
codec.decodeNumber(code);        // 1   (number; throws if > Number.MAX_SAFE_INTEGER)
```

## API surface

| Item | Notes |
|------|-------|
| `new Dealcode({ key, alphabet, minLength, maxLength, domain })` | immutable (frozen), safe for concurrent use |
| `codec.encode(n)` | accepts `number` or `bigint`; throws `CounterRangeError` out of range |
| `codec.decode(code) -> bigint` / `codec.decodeNumber(code) -> number` | throw `InvalidCodeError` for malformed input |
| Read-only properties | `alphabet`, `radix`, `minLength`, `maxLength`, `domain`, `capacity` (bigint) |
| `new CyclingDealcode({ key, alphabet, length, domain })` + `cycleOf(n)` | fixed-length cycling mode, SPEC §11 — see [the configuration guide](../guide/configuration.md#fixed-length-cycling-mode) |
| Exports | preset alphabet strings as `ALPHABETS`; errors extend `DealcodeError` (`ConfigError`, `CounterRangeError`, `InvalidCodeError`) |

## Tests

```sh
cd js && npm install && npm run build && npm test
```

Covers the official NIST FF1 sample vectors, every shared cross-language
vector, and behavioural/edge cases against the compiled output.
