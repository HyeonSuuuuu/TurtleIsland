-- 캐릭터 진행 저장 (방을 나갈 때 저장, 로그인 때 로드). 여러 번 실행해도 안전하다.
--   sqlcmd -S .\SQLEXPRESS -d TurtleIsland -E -C -i Server\Server\Sql\CharacterProgress.sql
IF OBJECT_ID(N'dbo.CharacterProgress', N'U') IS NULL
    CREATE TABLE dbo.CharacterProgress
    (
        AccountId    BIGINT        NOT NULL CONSTRAINT PK_CharacterProgress PRIMARY KEY
                                            CONSTRAINT FK_CharacterProgress_Account REFERENCES dbo.Account (AccountId),
        ProgressJson VARCHAR(MAX)  NOT NULL, -- level, exp, gold, items, shop_affinity (PlayerProgress.h)
        UpdatedAt    DATETIME2     NOT NULL CONSTRAINT DF_CharacterProgress_UpdatedAt DEFAULT SYSUTCDATETIME()
    );
GO

CREATE OR ALTER PROCEDURE dbo.usp_Character_Load
    @AccountId BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SELECT ProgressJson FROM dbo.CharacterProgress WHERE AccountId = @AccountId;
END
GO

CREATE OR ALTER PROCEDURE dbo.usp_Character_Save
    @AccountId    BIGINT,
    @ProgressJson VARCHAR(MAX)
AS
BEGIN
    SET NOCOUNT ON;
    UPDATE dbo.CharacterProgress SET ProgressJson = @ProgressJson, UpdatedAt = SYSUTCDATETIME() WHERE AccountId = @AccountId;
    IF @@ROWCOUNT = 0
        INSERT dbo.CharacterProgress (AccountId, ProgressJson) VALUES (@AccountId, @ProgressJson);
END
GO
