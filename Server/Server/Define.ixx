module;
#include <winSock2.h>

export module Define;
import Common;

export
{
	constexpr int16 MAX_SOCKBUF = 1024;
	constexpr uint32 ACCEPT_COUNT = 10;
	constexpr int64 REUSE_WAIT_MS = 2000;
	constexpr uint32 INVALID_SESSION_INDEX = UINT_MAX;
	constexpr uint32 INVALID_ID = UINT_MAX - 1;
	constexpr uint32 AI_ID = UINT_MAX - 2;
	enum class IOOperation : uint8
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
		uint32 sessionIndex;
		uint16 packetId;
		std::span<const byte> dataSpan;
	};
}
