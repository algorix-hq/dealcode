# Why dealcode exists

**English** | [한국어](philosophy.ko.md)

## The problem

Every product eventually needs a short, human-visible identifier: an order
number, a coupon code, an invite code, a ticket reference. Two requirements
collide immediately:

1. **It must be unique.** Two customers cannot share an order number.
2. **It must not reveal your internals.** `ORDER-000042` tells the world you
   have 42 orders, and lets anyone enumerate your order table. Estimating
   production from serial numbers is literally a WWII intelligence technique
   (the [German tank problem](https://en.wikipedia.org/wiki/German_tank_problem)).

The two standard non-solutions:

- **Random codes + retry on collision.** Randomness collides much earlier
  than intuition suggests: with 6-digit codes (1,000,000 possibilities) the
  first duplicate is *expected* around 1,200 issued codes — a birthday-paradox
  effect, not a rare event. So every issuance needs a uniqueness check, a
  retry loop, and a story for what happens under concurrency. As the space
  fills, retries snowball: at 50% fill, half of all attempts fail. This is a
  probabilistic algorithm doing a deterministic job.
- **Sequential IDs, exposed.** Free uniqueness, zero privacy. Base62-encoding
  a sequence or "adding a big offset" changes the costume, not the leak —
  consecutive orders still produce visibly related codes.

## The dealcode answer

Keep the sequence (databases produce non-repeating integers effortlessly and
concurrency-safely), and put a **keyed permutation** between the sequence and
the world:

```
sequence  ──►  FF1 encryption (your key)  ──►  code
   n                bijection on d digits        looks random, provably unique
```

A permutation is a shuffled deck: every input maps to exactly one output and
vice versa. Uniqueness is inherited from the sequence — *by construction*,
not by checking. The shuffle is AES-based FF1 (NIST SP 800-38G), so without
the key the output order is computationally indistinguishable from random.

Consequences worth internalizing:

- **No collision handling code exists in your codebase.** Not "collisions are
  rare" — the code path does not exist.
- **Issuance is a pure function.** No extra table, no coordination between
  app servers, nothing to cache or lock. `nextval()` + one encryption.
- **Codes stay minimal-length.** A random scheme must over-provision length to
  keep collision rates workable. A permutation uses its space fully: 16.7M
  six-character hex codes, then — only then — seven characters.
- **`decode` is free.** The code *is* the (encrypted) primary key. Lookup
  needs no secondary index, and *malformed* input (wrong length or alphabet)
  is rejected before the database sees it. Note the flip side: a well-formed
  code always decodes to *some* counter — decode is parsing, and the database
  lookup is what establishes existence.

## What to use it for

| Case | Why it fits |
|------|-------------|
| Order / invoice / shipment numbers | unique, short, hides volume & rate |
| Coupon & gift codes | non-guessing-critical, human-typable (Crockford preset) |
| Invite / referral codes | short, non-enumerable, decodes to the inviter row |
| Ticket / case / booking references | operators can read them over the phone |
| Short links | slug = encoded row id, no slug-collision logic |
| Obfuscated public ids | stop leaking `user_id=48291` in URLs without a UUID migration |

Common thread: **you already own a counter** (or trivially can), and the value
must be *unique and unrevealing* but not *secret-grade*.

## What NOT to use it for

- **Anything that authenticates by itself** — session tokens, API keys,
  password-reset and magic-login links. Dealcode's code space is deliberately
  small (that's what makes codes short). An attacker who can try codes online
  succeeds at rate `issued / capacity`. Use ≥128-bit CSPRNG tokens
  (`secrets.token_urlsafe(32)` and friends). Rate-limit lookups of dealcode
  codes as basic hygiene.
- **IDs without a central counter.** If independent machines must mint ids
  with no shared sequence, use UUIDv4/v7, ULID, or Snowflake. Dealcode's whole
  trick is leaning on the counter you already have.
- **Sortable identifiers.** Hiding order is the *feature*. Need
  time-sortable? UUIDv7/ULID.
- **One-time passwords.** OTPs verify against a per-user secret and tolerate
  global duplicates; that's HOTP/TOTP (RFC 4226/6238), a different problem.
- **Encrypting actual data.** FF1 on small domains is fine for obfuscating a
  counter, but it is not how you protect confidential fields.

### Alternatives cheat-sheet

| Tool | Guarantees | Reach for it when |
|------|-----------|-------------------|
| **dealcode** | zero collisions, hides sequence, decodable, minimal length | you have a counter; codes face humans |
| UUIDv4 | collision-negligible, coordination-free | machine-to-machine ids, no counter |
| UUIDv7 / ULID | as above + time-sortable | distributed + index-friendly |
| Snowflake | sortable, compact, coordination-light | high-throughput distributed ids |
| nanoid / `secrets` | secure randomness, any length | tokens that must be unguessable |
| sqids / hashids | short, reversible, **not cryptographic** | cosmetic obfuscation only, no key discipline |
| HOTP / TOTP | per-user one-time proof | authentication codes |

(On sqids/hashids: they solve the same "hide the integer" problem but with a
non-cryptographic shuffle — recovering the salt from a handful of codes is
feasible. Dealcode is the same developer experience with AES underneath.)

## Operational rules

1. **The configuration is write-once.** Key, alphabet, `min_length`,
   `max_length`, `domain` — frozen when the first code ships. A different
   config is a *different permutation*, and two permutations over one counter
   space collide. New scheme → new domain / namespace.
2. **One namespace, one counter.** Never feed two sequences into the same
   codec+domain, and never reset a sequence (`setval`) backwards.
3. **Keep a `UNIQUE` index on the stored code as a tripwire.** It can only
   fire if rule 1 or 2 was broken. Alert and investigate; never retry.
4. **Treat the key like a production secret.** It doesn't guard uniqueness
   (that's structural) but it guards *unlinkability* — leak it and every
   code's position in your sequence becomes public. KMS/Vault, per-environment
   keys, and note that rotation = new namespace (rule 1).
5. **Rate-limit public lookups.** Small code space; make online guessing
   boring.

## Name

Dealing cards from a shuffled deck: each card exactly once, order looks
random, and the dealer needs to remember nothing but a count. That is the
entire library.
