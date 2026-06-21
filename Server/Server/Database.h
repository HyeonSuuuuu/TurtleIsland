#pragma once
#include "pch.h"
#include <string>

// 로그인 결과
enum class AuthResult : uint8_t
{
	Ok,             // 기존 계정 비번 일치
	Registered,     // 신규 가입됨(첫 로그인)
	WrongPassword,  // 기존 계정인데 비번 불일치
	DbError,        // 쿼리/연결 오류
	Disabled,       // DB 미연결 — 호출측에서 폴백 처리
};

// MSSQL 접근(ODBC). 단일 연결 + 뮤텍스 직렬화(로그인은 드묾).
// 연결 실패해도 서버는 계속 동작하고, 인증은 호출측이 폴백한다.
class Database
{
public:
	bool Connect(const char* connStr);
	void Disconnect();
	bool IsConnected() const { return m_connected; }

	// 존재하면 비번 검증, 없으면 자동 가입. 성공 시 outAccountId 채움.
	AuthResult LoginOrRegister(const std::string& loginId, const std::string& password, uint64_t& outAccountId);

private:
	void* m_env = nullptr;      // SQLHENV (헤더에 odbc 의존을 두지 않으려 void*)
	void* m_dbc = nullptr;      // SQLHDBC
	bool  m_connected = false;
	std::mutex m_mutex;
};

inline Database g_db;

namespace DbConfig
{
	// 환경변수 TI_DB_CONN 로 덮어쓸 수 있음(Database 미연결 시 폴백).
	// DSN(TurtleIsland)을 만들어 두었으므로 그것을 사용. DSN에 서버/DB/드라이버가 포함됨.
	inline constexpr const char* DEFAULT_CONN = "DSN=TurtleIsland;";
	// DSN-less 대안(드라이버 직접 지정):
	//   "Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=TurtleIsland;Trusted_Connection=yes;"
}
