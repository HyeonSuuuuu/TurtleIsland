#pragma once
#include "pch.h"

// ==================== 네트워크 상수 =======================
constexpr int16_t  MAX_SOCKBUF           = 1024;       // 1회 송수신 버퍼(와이어 패킷 최대 255B 이므로 충분)
constexpr uint32_t ACCEPT_COUNT          = 10;
constexpr uint32_t MAX_WORKER_THREADS    = 64;         // EBR reservations 배열 크기
constexpr uint32_t MAX_CLIENTS           = 10000;      // protocol_2026.h MAX_PLAYERS 와 동일
constexpr uint32_t INVALID_SESSION_INDEX = UINT32_MAX;
constexpr uint32_t INVALID_ID            = UINT32_MAX - 1;
constexpr uint32_t AI_ID                 = UINT32_MAX - 2;

// 콘텐츠 상수 (GameDev)
constexpr uint32_t MAX_NPCS              = 200000;
constexpr uint32_t MAX_ROOMS            = 5000;
constexpr uint32_t MAX_ROOM_USERS       = 4;
constexpr uint32_t MAX_ROOM_NPCS        = 8;

// GameDev 와이어 헤더(PacketHeader): [0..1]=size(2바이트), [2..3]=id(2바이트), [4]=type
constexpr uint32_t PACKET_LEN_FIELD_SIZE = 2;          // size 필드(2바이트)
constexpr uint32_t PACKET_TYPE_OFFSET    = 2;          // id 시작 오프셋

enum class IOOperation : uint8_t
{
	RECV,
	SEND,
	ACCEPT,
};

struct OverlappedEx : public OVERLAPPED
{
	SOCKET      m_socketClient = INVALID_SOCKET;
	WSABUF      m_wsa{};
	IOOperation m_operation{};
	uint32_t    m_slotIndex    = INVALID_SESSION_INDEX;
	char        m_buf[MAX_SOCKBUF]{};
};

// 송신용 OverlappedEx 풀 (단순 스택)
// 10만 ≈ 109MB(슬롯당 ~1088B) 그래도 넘치면 new
struct SendPool
{
	static constexpr int32_t SIZE = 100000;

	OverlappedEx  m_pool[SIZE];
	OverlappedEx* m_stack[SIZE];
	int32_t       m_top = 0;
	std::mutex    m_mutex;

	void Init()
	{
		for (int32_t i = 0; i < SIZE; ++i)
			m_stack[i] = &m_pool[i];
		m_top = SIZE;
	}

	// 풀에 여유가 있으면 슬롯, 없으면 new
	OverlappedEx* Acquire()
	{
		{
			std::lock_guard lock(m_mutex);
			if (m_top > 0) return m_stack[--m_top];
		}
		return new OverlappedEx();   // 고갈 — Release 에서 풀 범위 밖이면 delete
	}

	void Release(OverlappedEx* p)
	{
		// 풀 배열 범위 밖은 해제
		if (p < &m_pool[0] || p > &m_pool[SIZE - 1]) { delete p; return; }
		std::lock_guard lock(m_mutex);
		m_stack[m_top++] = p;
	}
};

// 워커 → 콘텐츠로 넘기는 1개 패킷 packetId = PacketHeader.id(2바이트)
struct Packet
{
	uint32_t sessionIndex = 0;
	uint16_t packetId     = 0;
	std::span<const uint8_t> dataSpan;
};
