/* ============================================================
   거북섬 (Turtle Island) — Account 스키마  [MS SQL Server]
   실행: SSMS 에서 열어 실행하거나
         sqlcmd -S localhost -E -i 01_account.sql
   ============================================================ */

IF DB_ID(N'TurtleIsland') IS NULL
    CREATE DATABASE TurtleIsland;
GO

USE TurtleIsland;
GO

IF OBJECT_ID(N'dbo.Account', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.Account
    (
        AccountId   BIGINT        IDENTITY(1,1) NOT NULL,  -- 내부 식별자(향후 FK 대상)
        LoginId     NVARCHAR(32)  NOT NULL,                -- 로그인 ID 겸 표시 닉네임
        PwHash      VARCHAR(160)  NOT NULL,                -- "hex(salt):hex(sha256(salt+pw))"
        CONSTRAINT PK_Account            PRIMARY KEY (AccountId),
        CONSTRAINT UQ_Account_LoginId    UNIQUE (LoginId)
    );
    PRINT 'dbo.Account created.';
END
ELSE
    PRINT 'dbo.Account already exists.';
GO
