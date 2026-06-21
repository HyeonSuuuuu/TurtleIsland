/* ============================================================
   거북섬 — Account 저장 프로시저 (세이브 / 로드)  [MS SQL Server]
   실행: SSMS에서 TurtleIsland 선택 후 실행하거나
         sqlcmd -S localhost -E -d TurtleIsland -i 02_account_procs.sql
   ============================================================ */

USE TurtleIsland;
GO

/* ---------- 로드(Load): 로그인 시 계정 조회 ----------
   LoginId로 계정을 찾아 AccountId, PwHash 반환.
   없으면 0행 → 서버는 "신규 가입" 흐름으로 넘어감.            */
CREATE OR ALTER PROCEDURE dbo.usp_Account_Load
    @LoginId NVARCHAR(32)
AS
BEGIN
    SET NOCOUNT ON;   -- "n rows affected" 결과셋 억제(ODBC fetch 꼬임 방지)

    SELECT AccountId, PwHash
    FROM   dbo.Account
    WHERE  LoginId = @LoginId;
END
GO

/* ---------- 세이브(Save): 신규 계정 등록 ----------
   비밀번호 해시는 서버(C++)가 만들어 넘김(@PwHash).
   생성된 AccountId를 결과셋으로 반환.                          */
CREATE OR ALTER PROCEDURE dbo.usp_Account_Save
    @LoginId NVARCHAR(32),
    @PwHash  VARCHAR(160)
AS
BEGIN
    SET NOCOUNT ON;

    INSERT INTO dbo.Account (LoginId, PwHash)
    OUTPUT INSERTED.AccountId
    VALUES (@LoginId, @PwHash);
END
GO
