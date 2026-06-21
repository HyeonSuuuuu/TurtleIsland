# 서버 DB(MSSQL) 셋업

거북섬 게임 서버의 영속 데이터는 현재 **`ACCOUNT` 테이블 1개**뿐입니다. (전적·통계·친구는 보류)

## 1. 준비물
- **SQL Server** (Developer 또는 Express 무료 에디션)
- **ODBC Driver 17 (또는 18) for SQL Server**

## 2. 스키마 생성
SSMS로 `01_account.sql`을 열어 실행하거나:
```
sqlcmd -S localhost -E -i 01_account.sql
```
→ `TurtleIsland` DB와 `dbo.Account` 테이블이 생성됩니다.

## 3. 연결 문자열
서버는 기본값으로 아래를 사용합니다 (`Database.h`의 `DbConfig::DEFAULT_CONN`):
```
Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=TurtleIsland;Trusted_Connection=yes;
```
환경에 맞게 바꾸려면 **재컴파일 없이** 환경변수로 덮어쓸 수 있습니다:
```
set TI_DB_CONN=Driver={ODBC Driver 18 for SQL Server};Server=localhost\SQLEXPRESS;Database=TurtleIsland;Trusted_Connection=yes;TrustServerCertificate=yes;
```
- Driver 18은 기본이 암호화라 로컬은 보통 `TrustServerCertificate=yes` 필요
- SQL 인증이면 `Trusted_Connection=yes` 대신 `Uid=...;Pwd=...;`

## 4. 동작
- 서버 부팅 시 DB 연결을 시도합니다.
- **연결 실패해도 서버는 그대로 실행**되며, 로그인은 "누구나 통과"(프로토타입 폴백)로 동작합니다.
- 연결 성공 시 로그인 규칙: **존재하는 ID면 비밀번호 검증, 없으면 자동 가입**(첫 로그인 = 가입).
- 비밀번호는 평문 저장 안 함 — `salt + SHA-256` 해시(`hex(salt):hex(hash)`)로 저장.
