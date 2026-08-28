# 데이터베이스 연동

dealcode는 의도적으로 저장소에 무관합니다. DB와 직접 통신하지 않고 —
카운터를 코드로 바꾸는 일만 합니다. 필요한 것은 반복되지 않는 정수
하나뿐이고, 그건 DB가 이미 제일 잘 만듭니다.

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
보장하므로, 코드의 유일성은 카운터의 유일성으로 완전히 환원됩니다. 잠금도,
재시도 루프도, 충돌 처리 코드 경로도 없습니다.

MySQL 등은 `AUTO_INCREMENT`/identity 컬럼으로 같은 패턴을 쓰면 됩니다 —
반복되지 않는 정수 소스라면 무엇이든 좋습니다.

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
