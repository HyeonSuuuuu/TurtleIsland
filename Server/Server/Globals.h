#pragma once
#include "pch.h"
#include "Define.h"
#include "Pool.h"
#include "Session.h"
#include "User.h"
#include "Room.h"

// 풀에 담기는 세션 타입 = User. 프레임워크는 Session* 로만 접근.
using PooledSession = User;

inline Pool<PooledSession, MAX_CLIENTS>        g_userPool;
inline SendPool                                g_sendPool;
inline std::unordered_map<uint32_t, uint32_t>  g_idToIndexMap;   // GetId() -> pool index
inline std::array<Room, MAX_ROOMS>             g_rooms{};        // 방 인스턴스

// 중복 접속 차단용: 로그인된 userId -> slotIndex. g_onlineMutex 로 보호.
inline std::unordered_map<std::string, uint32_t> g_onlineUsers;
inline std::mutex                                g_onlineMutex;
inline Session*                                g_aiSession = nullptr;   // AI 서버 연결(미연결 시 nullptr)

inline HANDLE g_iocpHandle = INVALID_HANDLE_VALUE;               // 타이머/DB 스레드가 완료 게시에 사용
