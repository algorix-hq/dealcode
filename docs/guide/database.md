# Database integration

dealcode is deliberately storage-agnostic: it does not talk to your database
— it only turns a counter into a code. It needs a never-repeating integer,
which your database already knows how to produce.

## The recipe

With PostgreSQL:

```sql
CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;

CREATE TABLE orders (
  id   bigint PRIMARY KEY,          -- the counter
  code text NOT NULL UNIQUE,        -- safety net; alerts on config mistakes
  ...
);
```

On create: fetch `nextval()`, encode, insert. On lookup: decode, then select
by primary key — malformed codes never reach the database.

=== "Python"

    ```python
    codec = Dealcode(key=os.environ["DEALCODE_KEY"], domain="orders")

    def create_order(conn) -> str:
        n = conn.execute(text("SELECT nextval('order_code_seq')")).scalar_one()
        code = codec.encode(n)
        conn.execute(
            text("INSERT INTO orders (id, code) VALUES (:id, :code)"),
            {"id": n, "code": code},
        )
        return code

    def find_order(conn, code: str):
        try:
            n = codec.decode(code)          # malformed codes never reach the DB
        except InvalidCodeError:
            return None
        return conn.execute(text("SELECT * FROM orders WHERE id = :id"), {"id": n}).first()
    ```

=== "TypeScript / JavaScript"

    ```ts
    import { Dealcode, InvalidCodeError } from "dealcode";

    const codec = new Dealcode({ key: process.env.DEALCODE_KEY!, domain: "orders" });

    async function createOrder(db) {
      const { rows: [{ n }] } = await db.query("SELECT nextval('order_code_seq') AS n");
      const code = codec.encode(BigInt(n));
      await db.query("INSERT INTO orders (id, code) VALUES ($1, $2)", [n, code]);
      return code;
    }

    async function findOrder(db, code) {
      let n;
      try {
        n = codec.decode(code);           // malformed codes never reach the DB
      } catch (err) {
        if (err instanceof InvalidCodeError) return null;
        throw err;
      }
      const { rows } = await db.query("SELECT * FROM orders WHERE id = $1", [n]);
      return rows[0] ?? null;
    }
    ```

=== "Go"

    ```go
    codec, err := dealcode.New(dealcode.Config{
    	KeyString: os.Getenv("DEALCODE_KEY"),
    	Domain:    "orders",
    })

    func createOrder(ctx context.Context, db *sql.DB) (string, error) {
    	var n int64
    	if err := db.QueryRowContext(ctx, "SELECT nextval('order_code_seq')").Scan(&n); err != nil {
    		return "", err
    	}
    	code, err := codec.Encode(n)
    	if err != nil {
    		return "", err
    	}
    	_, err = db.ExecContext(ctx, "INSERT INTO orders (id, code) VALUES ($1, $2)", n, code)
    	return code, err
    }

    func findOrder(ctx context.Context, db *sql.DB, code string) (*Order, error) {
    	n, err := codec.Decode(code) // malformed codes never reach the DB
    	if errors.Is(err, dealcode.ErrInvalidCode) {
    		return nil, nil
    	}
    	// ... SELECT * FROM orders WHERE id = n
    }
    ```

=== "Java"

    ```java
    Dealcode codec = Dealcode.builder()
            .key(System.getenv("DEALCODE_KEY"))
            .domain("orders")
            .build();

    String createOrder(Connection conn) throws SQLException {
        long n;
        try (ResultSet rs = conn.createStatement()
                .executeQuery("SELECT nextval('order_code_seq')")) {
            rs.next();
            n = rs.getLong(1);
        }
        String code = codec.encode(n);
        try (PreparedStatement ps =
                conn.prepareStatement("INSERT INTO orders (id, code) VALUES (?, ?)")) {
            ps.setLong(1, n);
            ps.setString(2, code);
            ps.executeUpdate();
        }
        return code;
    }

    Optional<Order> findOrder(Connection conn, String code) {
        long n;
        try {
            n = codec.decode(code);      // malformed codes never reach the DB
        } catch (InvalidCodeException e) {
            return Optional.empty();
        }
        return findOrderById(conn, n);
    }
    ```

=== "Rust"

    ```rust
    use dealcode::{Dealcode, Error};

    let codec = Dealcode::builder(std::env::var("DEALCODE_KEY").unwrap())
        .domain("orders")
        .build()?;

    // on create:
    let n: i64 = client.query_one("SELECT nextval('order_code_seq')", &[])?.get(0);
    let code = codec.encode(n as u64)?;
    client.execute("INSERT INTO orders (id, code) VALUES ($1, $2)", &[&n, &code])?;

    // on lookup:
    fn find_order(client: &mut Client, codec: &Dealcode, code: &str) -> Option<Row> {
        let n = codec.decode(code).ok()?;   // malformed codes never reach the DB
        client.query_opt("SELECT * FROM orders WHERE id = $1", &[&(n as i64)]).ok()?
    }
    ```

=== "C / C++"

    The C and C++ APIs are the same `encode`/`decode` pair around whatever
    database driver you use — fetch `nextval()`, call `dealcode_encode`
    (or `codec.encode(n)` in C++), insert; on lookup call
    `dealcode_decode` first and hit the database only when it succeeds. See
    the [C](../languages/c.md) and [C++](../languages/cpp.md) pages for the
    API surface.

## Why this is safe

Sequences never hand out the same number twice — even across concurrent
transactions and rollbacks — so codes never collide. Gaps in the sequence are
invisible: codes look random anyway. And FF1 guarantees distinct inputs give
distinct outputs, so uniqueness of codes reduces entirely to uniqueness of
counters. No locks, no retry loop, no collision-handling code path.

Any source of never-repeating integers qualifies — see the
[per-database recipes](#per-database-recipes) below for how each major
engine provides one, and which engine-specific behaviors to avoid.

!!! warning "The `UNIQUE` index is a tripwire, not a mechanism"

    Keep a `UNIQUE` index on the stored code — but understand its role: it
    can only fire if the key or configuration changed for an existing
    namespace (or two counter sources were fed into one namespace). If it
    ever fires, **do not retry** — investigate. Retrying would paper over a
    configuration mistake that will keep colliding.

!!! note "Cycling mode changes the schema"

    Everything above assumes the default, ever-growing mode. In the
    [fixed-length cycling mode](configuration.md#fixed-length-cycling-mode)
    codes **repeat across cycles by design**, so a global `UNIQUE(code)`
    fires at every rollover and is the wrong contract. Store the cycle next
    to the code and scope uniqueness per cycle:

    ```sql
    CREATE TABLE bookings (
      id    bigint PRIMARY KEY,          -- the counter
      cycle bigint NOT NULL,             -- decode(code, cycle) needs this
      code  text   NOT NULL,
      UNIQUE (cycle, code)
    );
    ```

    Retire or expire cycle `e`'s rows before issuing from cycle `e+1`.

## Per-database recipes

What differs per engine is how you obtain a never-repeating integer — and
which engine-specific behaviors can silently break "never-repeating". The
rule that must survive every engine: **gaps are fine, reuse is fatal.** A
skipped counter is just a code that never gets issued (invisible — codes
look random anyway); a reused counter is the same code handed to two
customers.

With a standalone sequence you know the counter *before* the insert
(fetch → encode → insert, as in the recipe above). With an
auto-increment/identity column the counter exists only *after* the insert:
insert the row, read the generated id, encode, and store the code in the
same transaction.

=== "PostgreSQL"

    ```sql
    CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;
    -- or let the table own the counter:
    CREATE TABLE orders (
      id   bigint GENERATED ALWAYS AS IDENTITY (START WITH 0 MINVALUE 0) PRIMARY KEY,
      code text UNIQUE
    );
    ```

    `nextval()` is concurrency-safe and never re-issues a value, across
    crashes and rollbacks alike. With an identity column, use
    `INSERT … RETURNING id`, encode, then `UPDATE … SET code` in the same
    transaction.

    Never run `setval()` backwards, and never `TRUNCATE … RESTART IDENTITY`
    a table whose codes are still out in the world — both rewind the
    counter.

=== "MySQL"

    ```sql
    CREATE TABLE orders (
      id   BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
      code VARCHAR(32) UNIQUE
    ) ENGINE = InnoDB;
    ```

    Insert, read `LAST_INSERT_ID()`, encode, store the code in the same
    transaction. Counters start at 1, not 0 — fine; counter 0 simply goes
    unissued.

    !!! warning "MySQL < 8.0 can reuse ids after a restart"

        Before 8.0, InnoDB kept the auto-increment counter in memory and
        recomputed it as `MAX(id) + 1` on restart. Delete the newest rows,
        restart the server, and those ids — and therefore their codes — get
        issued again. MySQL 8.0+ persists the counter. On 5.7, either never
        delete the newest rows or drive codes from a separate counter
        table.

    `TRUNCATE TABLE` and `ALTER TABLE … AUTO_INCREMENT = n` with a lower
    `n` also rewind the counter — never on a live namespace.

=== "MariaDB"

    ```sql
    CREATE SEQUENCE order_code_seq MINVALUE 0 START WITH 0 NOCYCLE;
    SELECT NEXT VALUE FOR order_code_seq;
    ```

    MariaDB (10.3+) has real sequences — prefer them: sequence state is
    persisted, so it survives restarts. `AUTO_INCREMENT` also works, but
    MariaDB still recomputes the in-memory counter as `MAX(id) + 1` on
    restart (it did not adopt MySQL 8.0's persistence), so the
    delete-newest-rows-then-restart reuse hazard applies to **all** MariaDB
    versions. Crash-dropped `CACHE` values are just gaps — fine.

=== "SQLite"

    ```sql
    CREATE TABLE orders (
      id   INTEGER PRIMARY KEY AUTOINCREMENT,
      code TEXT UNIQUE
    );
    ```

    The `AUTOINCREMENT` keyword is **required** here, not optional style: a
    plain `INTEGER PRIMARY KEY` picks `max(rowid) + 1`, so deleting the
    newest row re-issues its id — and its code. `AUTOINCREMENT` (backed by
    the internal `sqlite_sequence` table) guarantees ids are never reused.
    Read the counter with `last_insert_rowid()`, and never edit or delete
    rows in `sqlite_sequence`.

=== "Oracle"

    ```sql
    CREATE SEQUENCE order_code_seq START WITH 0 MINVALUE 0 NOCYCLE;
    ```

    Use `order_code_seq.NEXTVAL` directly in the `INSERT` (or fetch it
    first). Values dropped from `CACHE` on a crash are gaps — fine. Never
    add `CYCLE`, and never drop-and-recreate the sequence with a lower
    `START WITH`. Identity columns (12c+,
    `GENERATED ALWAYS AS IDENTITY`) sit on a system sequence and behave the
    same; avoid `ALTER TABLE … MODIFY id … START WITH` restarts.

=== "SQL Server"

    ```sql
    CREATE SEQUENCE order_code_seq AS bigint START WITH 0 MINVALUE 0 NO CYCLE;
    -- INSERT INTO orders (id, code) VALUES (NEXT VALUE FOR order_code_seq, @code);
    ```

    Or `IDENTITY(0,1)` with `SCOPE_IDENTITY()` after the insert. Identity
    caching can skip a block of values after an unexpected restart (up to
    10,000 for `bigint`) — gaps, fine. Never
    `DBCC CHECKIDENT (orders, RESEED, n)` with a lower `n`, never
    `ALTER SEQUENCE … RESTART`, and remember `TRUNCATE TABLE` reseeds the
    identity — all three rewind the counter.

Whatever your ORM calls its id generation — Django's `AutoField`, JPA's
`@GeneratedValue`, ActiveRecord's `id`, Prisma's `autoincrement()` — it
maps to one of the mechanisms above, and the same rules apply underneath.

| Engine | Counter source | Harmless (gaps) | Fatal (reuse) — never on a live namespace |
|--------|----------------|-----------------|-------------------------------------------|
| PostgreSQL | `SEQUENCE` / identity | rollbacks, crash-dropped cache | `setval()` backwards, `TRUNCATE … RESTART IDENTITY` |
| MySQL | `AUTO_INCREMENT` | rollbacks, failed inserts | `TRUNCATE`, lowering `AUTO_INCREMENT`; < 8.0: delete newest rows + restart |
| MariaDB | `SEQUENCE` (10.3+) preferred | crash-dropped cache | as MySQL — the restart recomputation applies to all versions |
| SQLite | `INTEGER PRIMARY KEY AUTOINCREMENT` | none in practice | omitting `AUTOINCREMENT`, touching `sqlite_sequence` |
| Oracle | `SEQUENCE` / identity (12c+) | crash-dropped `CACHE` | `CYCLE`, recreating the sequence lower, identity restart |
| SQL Server | `SEQUENCE` / `IDENTITY` | identity cache after restart | `DBCC CHECKIDENT RESEED` lower, `ALTER SEQUENCE … RESTART`, `TRUNCATE` |

## Decode is parsing, not proof of existence

A *well-formed* code always decodes to some counter, whether or not that
counter was ever issued (inherent to a permutation). The database lookup is
what establishes existence. A one-character typo in a valid code can resolve
to a *different* valid counter, so rate-limit public lookups, and for
human-typed flows add an existence check or your own check digit.

This applies **across domains of one key** too: in a multi-tenant setup
(one key, one domain per tenant), tenant A's code will "successfully"
decode under tenant B's codec — to a counter B never issued. The
existence check above is what keeps tenants isolated; don't skip it.
