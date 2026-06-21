#include "pch.h"
#include "Database.h"
#include <sql.h>
#include <sqlext.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "odbc32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace
{
	std::wstring Utf8ToWide(const std::string& s)
	{
		if (s.empty()) return L"";
		int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	std::string ToHex(const uint8_t* d, size_t n)
	{
		static const char* H = "0123456789abcdef";
		std::string out; out.reserve(n * 2);
		for (size_t i = 0; i < n; ++i) { out.push_back(H[d[i] >> 4]); out.push_back(H[d[i] & 0xF]); }
		return out;
	}

	bool GenSalt(uint8_t* out, size_t n)
	{
		return BCryptGenRandom(nullptr, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
	}

	// SHA-256(salt || pw) → hex
	bool Sha256Hex(const uint8_t* salt, size_t saltLen, const std::string& pw, std::string& hexOut)
	{
		BCRYPT_ALG_HANDLE alg = nullptr;
		if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
		bool ok = false;
		BCRYPT_HASH_HANDLE h = nullptr;
		if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0)
		{
			if (BCryptHashData(h, (PUCHAR)salt, (ULONG)saltLen, 0) == 0 &&
				BCryptHashData(h, (PUCHAR)pw.data(), (ULONG)pw.size(), 0) == 0)
			{
				uint8_t digest[32];
				if (BCryptFinishHash(h, digest, sizeof(digest), 0) == 0)
				{
					hexOut = ToHex(digest, sizeof(digest));
					ok = true;
				}
			}
			BCryptDestroyHash(h);
		}
		BCryptCloseAlgorithmProvider(alg, 0);
		return ok;
	}

	// 저장 포맷: hex(salt) ":" hex(sha256(salt+pw))
	std::string MakeHash(const std::string& pw)
	{
		uint8_t salt[16];
		if (!GenSalt(salt, sizeof(salt))) return "";
		std::string hh;
		if (!Sha256Hex(salt, sizeof(salt), pw, hh)) return "";
		return ToHex(salt, sizeof(salt)) + ":" + hh;
	}

	bool VerifyHash(const std::string& pw, const std::string& stored)
	{
		size_t pos = stored.find(':');
		if (pos == std::string::npos || pos % 2 != 0) return false;
		std::vector<uint8_t> salt(pos / 2);
		for (size_t i = 0; i < salt.size(); ++i)
			salt[i] = (uint8_t)std::stoul(stored.substr(i * 2, 2), nullptr, 16);
		std::string hh;
		if (!Sha256Hex(salt.data(), salt.size(), pw, hh)) return false;
		return hh == stored.substr(pos + 1);
	}

	void LogOdbcError(const char* where, SQLSMALLINT type, SQLHANDLE h)
	{
		SQLCHAR state[6] = {}, msg[256] = {};
		SQLINTEGER native = 0; SQLSMALLINT len = 0;
		if (SQLGetDiagRecA(type, h, 1, state, &native, msg, sizeof(msg), &len) == SQL_SUCCESS)
			Log::Warning("[DB] {} 실패: [{}] {}", where, (const char*)state, (const char*)msg);
		else
			Log::Warning("[DB] {} 실패", where);
	}
}

bool Database::Connect(const char* connStr)
{
	std::lock_guard lock(m_mutex);
	if (m_connected) return true;

	SQLHENV env = nullptr; SQLHDBC dbc = nullptr;
	if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) != SQL_SUCCESS) return false;
	SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	if (SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) != SQL_SUCCESS)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, env);
		return false;
	}

	SQLCHAR outStr[1024] = {}; SQLSMALLINT outLen = 0;
	SQLRETURN r = SQLDriverConnectA(dbc, nullptr, (SQLCHAR*)connStr, SQL_NTS,
		outStr, sizeof(outStr), &outLen, SQL_DRIVER_NOPROMPT);
	if (r != SQL_SUCCESS && r != SQL_SUCCESS_WITH_INFO)
	{
		LogOdbcError("Connect", SQL_HANDLE_DBC, dbc);
		SQLFreeHandle(SQL_HANDLE_DBC, dbc);
		SQLFreeHandle(SQL_HANDLE_ENV, env);
		Log::Warning("[DB] 연결 실패 — 로그인 폴백(누구나 통과)으로 동작");
		return false;
	}

	m_env = env; m_dbc = dbc; m_connected = true;
	Log::Success("[DB] MSSQL 연결 성공");
	return true;
}

void Database::Disconnect()
{
	std::lock_guard lock(m_mutex);
	if (m_dbc) { SQLDisconnect((SQLHDBC)m_dbc); SQLFreeHandle(SQL_HANDLE_DBC, m_dbc); m_dbc = nullptr; }
	if (m_env) { SQLFreeHandle(SQL_HANDLE_ENV, m_env); m_env = nullptr; }
	m_connected = false;
}

AuthResult Database::LoginOrRegister(const std::string& loginId, const std::string& password, uint64_t& outAccountId)
{
	std::lock_guard lock(m_mutex);
	if (!m_connected) return AuthResult::Disabled;

	SQLHDBC dbc = (SQLHDBC)m_dbc;
	std::wstring wLogin = Utf8ToWide(loginId);

	// 1) 기존 계정 조회
	SQLHSTMT stmt = nullptr;
	if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) return AuthResult::DbError;

	SQLLEN lenLogin = SQL_NTS;
	SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 32, 0,
		(SQLPOINTER)wLogin.c_str(), 0, &lenLogin);

	const wchar_t* sel = L"{CALL dbo.usp_Account_Load(?)}";   // 로드(조회) SP
	if (SQLExecDirectW(stmt, (SQLWCHAR*)sel, SQL_NTS) == SQL_ERROR)
	{
		LogOdbcError("Select", SQL_HANDLE_STMT, stmt);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return AuthResult::DbError;
	}

	SQLRETURN fr = SQLFetch(stmt);
	if (fr == SQL_SUCCESS || fr == SQL_SUCCESS_WITH_INFO)
	{
		SQLBIGINT id = 0; SQLLEN idLen = 0;
		char pwHash[200] = {}; SQLLEN hashLen = 0;
		SQLGetData(stmt, 1, SQL_C_SBIGINT, &id, 0, &idLen);
		SQLGetData(stmt, 2, SQL_C_CHAR, pwHash, sizeof(pwHash), &hashLen);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);

		if (VerifyHash(password, pwHash)) { outAccountId = (uint64_t)id; return AuthResult::Ok; }
		return AuthResult::WrongPassword;
	}
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

	// 2) 미존재 → 신규 가입
	std::string stored = MakeHash(password);
	if (stored.empty()) return AuthResult::DbError;

	SQLHSTMT ins = nullptr;
	if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &ins) != SQL_SUCCESS) return AuthResult::DbError;

	SQLLEN l1 = SQL_NTS, l2 = SQL_NTS;
	SQLBindParameter(ins, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 32, 0,
		(SQLPOINTER)wLogin.c_str(), 0, &l1);
	SQLBindParameter(ins, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 160, 0,
		(SQLPOINTER)stored.c_str(), 0, &l2);

	const wchar_t* insSql = L"{CALL dbo.usp_Account_Save(?, ?)}";   // 세이브(등록) SP
	if (SQLExecDirectW(ins, (SQLWCHAR*)insSql, SQL_NTS) == SQL_ERROR)
	{
		LogOdbcError("Insert", SQL_HANDLE_STMT, ins);
		SQLFreeHandle(SQL_HANDLE_STMT, ins);
		return AuthResult::DbError;
	}

	AuthResult result = AuthResult::DbError;
	if (SQLFetch(ins) == SQL_SUCCESS)
	{
		SQLBIGINT newId = 0; SQLLEN nl = 0;
		SQLGetData(ins, 1, SQL_C_SBIGINT, &newId, 0, &nl);
		outAccountId = (uint64_t)newId;
		result = AuthResult::Registered;
		Log::Info("[DB] 신규 가입 loginId={} accountId={}", loginId, (long long)newId);
	}
	SQLFreeHandle(SQL_HANDLE_STMT, ins);
	return result;
}
