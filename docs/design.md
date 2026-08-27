# Design decisions

**English** | [한국어](design.ko.md)

A record of the choices behind format v1 and why the rejected options were
rejected. Read [philosophy.md](philosophy.md) first for the problem statement;
[SPEC.md](../SPEC.md) is the normative artifact.

## 1. Counter + permutation, not random + retry

A code scheme must be a bijection from "things issued so far" to codes, or
collisions are possible. Random generation approximates a bijection
probabilistically and pays for it forever (uniqueness checks, retry loops,
growing failure rates — the birthday bound puts the first expected duplicate
at ~√capacity). A database sequence already *is* a collision-free enumerator;
composing it with a keyed permutation preserves that property while destroying
the visible order. Uniqueness becomes a theorem instead of a probability.

## 2. FF1 as the permutation

Candidate permutations over `[0, r^d)` considered:

| Option | Verdict |
|--------|---------|
| Full-cycle LCG (Hull–Dobell) | keyless — anyone who sees one value and guesses the parameters predicts the rest; low-order digits cycle with short periods |
| Quadratic residue (Preshing) trick | keyless in spirit (the "key" is a small offset), limited domains (`p ≡ 3 mod 4`), weak mixing |
| Custom balanced Feistel + cycle walking | sound approach, but hand-rolled round counts/PRFs invite subtle interop and security bugs |
| Fisher–Yates table | true random permutation but requires storing the whole shuffled table and syncing it across services |
| **FF1 (NIST SP 800-38G)** | standardized, AES-based, tweakable, works natively on any radix and length, official test vectors exist |

FF1 wins on three grounds: it is a *published standard* with sample vectors
(so seven implementations can be proven identical), it is keyed with AES (so
unpredictability reduces to AES), and its radix/length flexibility maps 1:1
onto dealcode's alphabet/staging model with no cycle-walking needed.

FF3-1 was rejected: FF1 has the stronger security record (FF3 required a
revision after the 2017 Durak–Vaudenay attack) and FF1's free-form tweak
length fits the domain-string design; FF3-1 fixes the tweak at 56 bits.

Known limitation, accepted deliberately: distinguishing attacks on
small-domain FPE exist far below AES security levels. Dealcode's threat model
is "don't let outsiders read or enumerate the sequence", not "encrypt secrets";
for that model FF1 is comfortably sufficient, and anything stronger would
still leave the small code space enumerable online (mitigated by rate
limiting, not cryptography).

## 3. Length staging (why codes can grow)

Fixed-length schemes force a day-one guess about lifetime volume: too short
and you migrate painfully later; too long and every code carries dead weight
forever. Staging removes the guess: stage `d` covers counters
`[r^(d−1), r^d)` (first stage from 0), so codes are as short as the *current*
volume allows and lengths never collide with each other. The per-stage
capacity being "the rest of the digit range" (not the full `r^d`) is what
makes the counter→length function trivial (`d = digit count, floored at
min_length`) and gap-free.

The alternative — one huge fixed domain with cycle walking — was rejected
because it forces maximum-length codes from the first issue.

## 4. Tweaks and domains

FF1's tweak input provides permutation separation without key proliferation.
v1 binds `"dealcode/v1/" + domain`:

- the constant prefix separates dealcode from any other FF1 use of the same
  key, and format v1 from any future v2;
- `domain` gives applications unlimited independent namespaces under one key
  (orders vs coupons), which is operationally much cheaper than one key per
  namespace.

Radix and length need no tweak binding — FF1 already mixes both into its
round function derivation (the `P` block).

## 5. The 2^63 counter bound

Counters live in `[0, min(r^max_length, 2^63))` *by specification*. Reasons:

- Every real counter source (Postgres `bigserial`, MySQL `BIGINT
  AUTO_INCREMENT`, Snowflake sequence part) is a signed 64-bit integer, so
  nothing reachable is excluded.
- Making the bound part of the spec (not implementation-defined) means Java
  (`long`), Go (`int64`), C (`uint64_t` + explicit check) and Python
  (unbounded `int`) all accept and reject exactly the same inputs.
- Code space larger than counter space is still allowed
  (`r^max_length ≤ 2^128`) so fixed-length and aesthetic-length formats work;
  the unreachable region is rejected by decode as a range violation, the same
  way a wrong length or character set is.

## 6. Key material handling

Requiring "exactly 16/24/32 bytes" would push every user into writing their
own ad-hoc hex-decode-or-hash glue — the classic source of cross-service
mismatch. v1 fixes one rule in the spec: AES-sized byte strings pass through;
everything else (including *all* strings, even hex-looking ones — no
guessing) derives an AES-256 key via
`SHA-256("dealcode/v1/kdf" ‖ material)`. Plain SHA-256 with a fixed prefix is
sufficient because the input is key material, not a low-entropy password to
stretch; the prefix is domain separation. HKDF would add ceremony without
changing the security story here.

## 7. Cryptographic dependencies

Rule: **implement FF1 from the NIST text; never implement AES.** Each
language uses its platform-standard, audited AES: PyCA `cryptography`,
`node:crypto`, Go stdlib, JCE, RustCrypto `aes`/`sha2`, OpenSSL libcrypto.
No other runtime dependencies anywhere — supply-chain surface is the
platform's crypto library, full stop. FF1-from-spec is verified against the
official NIST sample vectors in every language, which is a stronger guarantee
than depending on any third-party FF1 package would give (few exist, fewer
are maintained, none are uniformly available across seven languages).

## 8. Monorepo with a vector contract

The product of this repository is a *bit-exact mapping*, so the main failure
mode is drift between implementations. Defenses, in order of importance:

1. `testvectors/v1.json` — generated by the Python reference, consumed by
   every implementation's test suite. Covers every preset, stage boundaries,
   key-derivation paths, normalization, and codes that MUST be rejected.
2. `testvectors/ff1_nist.json` — pins the FF1 core to the official NIST
   samples independent of dealcode's own layer.
3. One repo, so a spec change and all seven implementation changes land in a
   single reviewable commit.

A spec change that alters any mapping output is a **new format version**
(new tweak prefix), not an edit to v1.

## 9. Naming

`dealcode` — dealing cards from a shuffled deck: every card exactly once,
order looks random, dealer remembers only a count. The metaphor is load-
bearing; it explains the entire design in one sentence.
