#pragma once
#include "pch.h"

constexpr int16_t  MAX_SOCKBUF           = 1024;
constexpr uint32_t ACCEPT_COUNT          = 10;
constexpr int64_t  REUSE_WAIT_MS         = 2000;
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
	SOCKET      m_socketClient;
	WSABUF      m_wsa;
	IOOperation m_operation;
	char        m_buf[MAX_SOCKBUF]{};
};

struct Packet
{
	uint32_t sessionIndex;
	uint16_t packetId;
	std::span<const uint8_t> dataSpan;
};
