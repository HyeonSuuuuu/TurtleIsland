#pragma once
#include "pch.h"

constexpr int16_t  MAX_SOCKBUF           = 1024;
constexpr uint32_t ACCEPT_COUNT          = 10;
constexpr uint32_t MAX_WORKER_THREADS    = 64;
constexpr uint32_t MAX_NPCS              = 200000;
constexpr uint32_t MAX_ROOMS             = 5000;
constexpr uint32_t MAX_ROOM_USERS        = 4;
constexpr uint32_t MAX_ROOM_NPCS         = 8;
constexpr uint32_t INVALID_SESSION_INDEX = UINT32_MAX;
constexpr uint32_t INVALID_ID            = UINT32_MAX - 1;
constexpr uint32_t AI_ID                 = UINT32_MAX - 2;

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

struct SendPool
{
	static constexpr int32_t SIZE = 1024;

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

	OverlappedEx* Acquire()
	{
		std::lock_guard lock(m_mutex);
		if (m_top == 0) return nullptr;
		return m_stack[--m_top];
	}

	void Release(OverlappedEx* p)
	{
		std::lock_guard lock(m_mutex);
		m_stack[m_top++] = p;
	}
};

struct Packet
{
	uint32_t sessionIndex = 0;
	uint16_t packetId     = 0;
	std::span<const uint8_t> dataSpan;
};
