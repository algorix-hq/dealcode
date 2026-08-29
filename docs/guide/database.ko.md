# 데이터베이스 연동

dealcode는 의도적으로 저장소에 무관합니다. DB와 직접 통신하지 않고 —
카운터를 코드로 바꾸는 일만 합니다. 반복되지 않는 정수
하나면 되고, 그건 DB가 이미 제일 잘 만듭니다.

## 레시피

PostgreSQL 기준:

```sql
CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;

CREATE TABLE orders (
  id   bigint PRIMARY KEY,          -- 카운터
  code text NOT NULL UNIQUE,        -- 안전망; 설정 실수 시 경보
  ...
);
```

생성 시: `nextval()`을 받아 encode하고 insert. 조회 시: decode한 뒤 기본
키로 select — 형식이 잘못된 코드는 DB에 닿지 않습니다.

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
            n = codec.decode(code)          # 잘못된 코드는 DB에 닿지 않음
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
        n = codec.decode(code);           // 잘못된 코드는 DB에 닿지 않음
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
    	n, err := codec.Decode(code) // 잘못된 코드는 DB에 닿지 않음
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
            n = codec.decode(code);      // 잘못된 코드는 DB에 닿지 않음
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

    // 생성 시:
    let n: i64 = client.query_one("SELECT nextval('order_code_seq')", &[])?.get(0);
    let code = codec.encode(n as u64)?;
    client.execute("INSERT INTO orders (id, code) VALUES ($1, $2)", &[&n, &code])?;

    // 조회 시:
    fn find_order(client: &mut Client, codec: &Dealcode, code: &str) -> Option<Row> {
        let n = codec.decode(code).ok()?;   // 잘못된 코드는 DB에 닿지 않음
        client.query_opt("SELECT * FROM orders WHERE id = $1", &[&(n as i64)]).ok()?
    }
    ```

=== "C / C++"

    C와 C++도 같은 `encode`/`decode` 쌍을 사용하는 DB 드라이버 주위에 그대로
    두르면 됩니다 — `nextval()`을 받아 `dealcode_encode`(C++는
    `codec.encode(n)`)를 호출해 insert하고, 조회 시에는 `dealcode_decode`가
    성공했을 때만 DB에 접근합니다. API는 [C](../languages/c.ko.md)와
    [C++](../languages/cpp.ko.md) 페이지를 보세요.

## 왜 안전한가

시퀀스는 동시 트랜잭션과 롤백이 있어도 같은 번호를 두 번 주지 않으므로
코드는 절대 충돌하지 않습니다. 시퀀스의 갭은 티가 나지 않습니다 — 코드는
어차피 랜덤처럼 보이니까요. 그리고 FF1은 다른 입력에 다른 출력을
보장하므로 코드의 유일성은 카운터의 유일성으로 완전히 환원됩니다. 잠금도,
재시도 루프도, 충돌 처리 코드 경로도 없습니다.

반복되지 않는 정수 소스라면 무엇이든 좋습니다 — 메이저 엔진별로 그런
소스를 얻는 방법과 피해야 할 엔진별 동작은 아래
[DB별 레시피](#db-recipes)를 보세요.

!!! warning "`UNIQUE` 인덱스는 메커니즘이 아니라 경보 장치"

    저장된 코드에 `UNIQUE` 인덱스를 유지하되, 역할을 정확히 이해하세요:
    이 인덱스는 운영 중인 네임스페이스의 키나 설정이 바뀌었을 때(또는 두
    카운터 소스가 한 네임스페이스에 흘러들었을 때)만 울릴 수 있습니다.
    울렸다면 **재시도하지 말고** 조사하세요. 재시도는 계속 충돌할 설정
    실수를 덮을 뿐입니다.

!!! note "순환 모드에서는 스키마가 달라집니다"

    위 내용은 기본(길이가 늘어나는) 모드 기준입니다.
    [고정 길이 순환 모드](configuration.ko.md#fixed-length-cycling-mode)에서는
    코드가 **설계상 사이클마다 반복**되므로, 전역 `UNIQUE(code)`는 롤오버
    때마다 울리는 잘못된 계약입니다. 코드 옆에 사이클을 저장하고 유일성을
    사이클 단위로 잡으세요:

    ```sql
    CREATE TABLE bookings (
      id    bigint PRIMARY KEY,          -- 카운터
      cycle bigint NOT NULL,             -- decode(code, cycle)에 필요
      code  text   NOT NULL,
      UNIQUE (cycle, code)
    );
    ```

    사이클 `e+1`에서 발급을 시작하기 전에 사이클 `e`의 행들을 만료·회수하세요.

## DB별 레시피 { #db-recipes }

엔진마다 다른 것은 반복되지 않는 정수를 얻는 방법 — 그리고 "반복되지
않음"을 조용히 깨뜨릴 수 있는 엔진별 동작입니다. 어느 엔진에서든 지켜야
할 규칙은 하나입니다: **갭은 괜찮고, 재사용은 치명적입니다.** 건너뛴
카운터는 발급되지 않은 코드일 뿐이고(어차피 코드는 랜덤처럼 보여서 티도
안 납니다), 재사용된 카운터는 같은 코드가 두 고객에게 나간다는 뜻입니다.

독립 시퀀스는 insert *전에* 카운터를 알 수 있습니다(위 레시피처럼 조회 →
encode → insert). auto-increment/identity 컬럼은 insert *후에야* 카운터가
생기므로: 행을 insert하고, 생성된 id를 읽고, encode해서 같은 트랜잭션
안에서 코드를 저장하세요.

=== "PostgreSQL"

    ```sql
    CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;
    -- 또는 테이블이 카운터를 소유하게:
    CREATE TABLE orders (
      id   bigint GENERATED ALWAYS AS IDENTITY (START WITH 0 MINVALUE 0) PRIMARY KEY,
      code text UNIQUE
    );
    ```

    `nextval()`은 동시성에 안전하고 크래시와 롤백을 겪어도 같은 값을 두
    번 주지 않습니다. identity 컬럼이면 `INSERT … RETURNING id`로 받아
    encode한 뒤 같은 트랜잭션에서 `UPDATE … SET code` 하세요.

    `setval()`을 뒤로 돌리거나, 코드가 이미 세상에 나가 있는 테이블에
    `TRUNCATE … RESTART IDENTITY`를 실행하면 안 됩니다 — 둘 다 카운터를
    되감습니다.

=== "MySQL"

    ```sql
    CREATE TABLE orders (
      id   BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
      code VARCHAR(32) UNIQUE
    ) ENGINE = InnoDB;
    ```

    insert하고 `LAST_INSERT_ID()`를 읽어 encode한 뒤 같은 트랜잭션에서
    코드를 저장하세요. 카운터가 0이 아니라 1부터 시작하지만 괜찮습니다 —
    카운터 0이 발급되지 않을 뿐입니다.

    !!! warning "MySQL < 8.0은 재시작 후 id를 재사용할 수 있습니다"

        8.0 이전 InnoDB는 auto-increment 카운터를 메모리에만 두고
        재시작 시 `MAX(id) + 1`로 다시 계산했습니다. 최신 행들을 삭제하고
        서버를 재시작하면 그 id들 — 곧 그 코드들 — 이 다시 발급됩니다.
        MySQL 8.0+는 카운터를 영속화합니다. 5.7이라면 최신 행을 절대
        삭제하지 않거나, 별도 카운터 테이블로 코드를 발급하세요.

    `TRUNCATE TABLE`도, 값을 더 낮추는 `ALTER TABLE … AUTO_INCREMENT = n`도
    카운터를 되감습니다 — 운영 중인 네임스페이스에서는 금지입니다.

=== "MariaDB"

    ```sql
    CREATE SEQUENCE order_code_seq MINVALUE 0 START WITH 0 NOCYCLE;
    SELECT NEXT VALUE FOR order_code_seq;
    ```

    MariaDB(10.3+)에는 진짜 시퀀스가 있으니 그걸 쓰세요: 시퀀스 상태는
    영속화되어 재시작을 견딥니다. `AUTO_INCREMENT`도 동작하지만 MariaDB는
    재시작 시 메모리 카운터를 여전히 `MAX(id) + 1`로 재계산합니다(MySQL
    8.0의 영속화를 채택하지 않았습니다). 즉 최신-행-삭제-후-재시작 재사용
    위험이 **모든** MariaDB 버전에 적용됩니다. 크래시로 날아간 `CACHE`
    값들은 그냥 갭입니다 — 괜찮습니다.

=== "SQLite"

    ```sql
    CREATE TABLE orders (
      id   INTEGER PRIMARY KEY AUTOINCREMENT,
      code TEXT UNIQUE
    );
    ```

    여기서 `AUTOINCREMENT` 키워드는 스타일이 아니라 **필수**입니다: 그냥
    `INTEGER PRIMARY KEY`는 `max(rowid) + 1`을 고르므로 최신 행을
    삭제하면 그 id — 곧 그 코드 — 가 재발급됩니다. `AUTOINCREMENT`(내부
    `sqlite_sequence` 테이블 기반)는 id가 절대 재사용되지 않음을
    보장합니다. 카운터는 `last_insert_rowid()`로 읽고, `sqlite_sequence`의
    행은 절대 수정·삭제하지 마세요.

=== "Oracle"

    ```sql
    CREATE SEQUENCE order_code_seq START WITH 0 MINVALUE 0 NOCYCLE;
    ```

    `order_code_seq.NEXTVAL`을 `INSERT`에서 바로 쓰거나 먼저 조회하세요.
    크래시로 `CACHE`에서 날아간 값들은 갭입니다 — 괜찮습니다. `CYCLE`을
    붙이거나, 더 낮은 `START WITH`로 시퀀스를 지웠다 다시 만들면 안
    됩니다. identity 컬럼(12c+, `GENERATED ALWAYS AS IDENTITY`)은 시스템
    시퀀스 위에 있어 동작이 같습니다.
    `ALTER TABLE … MODIFY id … START WITH` 재시작은 피하세요.

=== "SQL Server"

    ```sql
    CREATE SEQUENCE order_code_seq AS bigint START WITH 0 MINVALUE 0 NO CYCLE;
    -- INSERT INTO orders (id, code) VALUES (NEXT VALUE FOR order_code_seq, @code);
    ```

    또는 `IDENTITY(0,1)` + insert 후 `SCOPE_IDENTITY()`. identity 캐시는
    비정상 재시작 후 값 블록을 건너뛸 수 있습니다(`bigint`는 최대
    10,000개) — 갭이니 괜찮습니다. `n`을 더 낮게 주는
    `DBCC CHECKIDENT (orders, RESEED, n)`와 `ALTER SEQUENCE … RESTART`는
    금지이고 `TRUNCATE TABLE`이 identity를 리시드한다는 것도 기억하세요
    — 셋 다 카운터를 되감습니다.

ORM이 id 생성을 뭐라고 부르든 — Django의 `AutoField`, JPA의
`@GeneratedValue`, ActiveRecord의 `id`, Prisma의 `autoincrement()` — 결국
위 메커니즘 중 하나로 내려가고 같은 규칙이 그대로 적용됩니다.

| 엔진 | 카운터 소스 | 무해 (갭) | 치명 (재사용) — 운영 네임스페이스에서 금지 |
|------|-------------|-----------|---------------------------------------------|
| PostgreSQL | `SEQUENCE` / identity | 롤백, 크래시로 날아간 캐시 | `setval()` 되감기, `TRUNCATE … RESTART IDENTITY` |
| MySQL | `AUTO_INCREMENT` | 롤백, 실패한 insert | `TRUNCATE`, `AUTO_INCREMENT` 낮추기; < 8.0: 최신 행 삭제 + 재시작 |
| MariaDB | `SEQUENCE`(10.3+) 권장 | 크래시로 날아간 캐시 | MySQL과 동일 — 재시작 재계산은 전 버전에 적용 |
| SQLite | `INTEGER PRIMARY KEY AUTOINCREMENT` | 사실상 없음 | `AUTOINCREMENT` 생략, `sqlite_sequence` 손대기 |
| Oracle | `SEQUENCE` / identity (12c+) | 크래시로 날아간 `CACHE` | `CYCLE`, 시퀀스를 낮춰 재생성, identity 재시작 |
| SQL Server | `SEQUENCE` / `IDENTITY` | 재시작 후 identity 캐시 | `DBCC CHECKIDENT RESEED` 낮추기, `ALTER SEQUENCE … RESTART`, `TRUNCATE` |

## decode는 존재 증명이 아니라 파싱

*형식이 올바른* 코드는 실제로 발급됐는지와 무관하게 항상 어떤 카운터로
복호화됩니다(순열의 본질). 존재를 확정하는 것은 DB 조회입니다. 유효한
코드의 한 글자 오타가 *다른* 유효한 카운터로 풀릴 수 있으니, 공개 조회에는
레이트 리밋을 걸고, 사람이 입력하는 플로우에는 존재 확인이나 자체 체크
디지트를 더하세요.

이 원칙은 **한 키의 도메인 사이에서도** 그대로 적용됩니다: 멀티테넌트
구성(키 하나 + 테넌트별 도메인)에서 테넌트 A의 코드는 테넌트 B의 코덱으로도
"성공적으로" 복호화됩니다 — B가 발급한 적 없는 카운터로요. 테넌트를
격리하는 것은 위의 존재 확인입니다. 생략하지 마세요.
