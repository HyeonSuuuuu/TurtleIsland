#pragma once
#include "pch.h"
#include "Define.h"
#include "GameObject.h"
#include "../../Common/Protocol.h"

enum class SessionState : uint8_t
{
	None      = 0,
	Retiring  = 1,
	Connected = 2,
};

class Session : public GameObject
{
public:
	static constexpr uint32_t PACKET_DATA_BUFFER_SIZE = 8096;

	std::atomic<SessionState> state{ SessionState::None };

	Session() = default;
	virtual ~Session()
	{
		delete[] m_packetDataBuffer;
	}

	virtual void Init(uint32_t id) override
	{
		GameObject::Init(id);
		m_packetDataBuffer = new uint8_t[PACKET_DATA_BUFFER_SIZE];
	}

	virtual void InitClear() override
	{
		m_packetDataReadPos  = 0;
		m_packetDataWritePos = 0;
		state.store(SessionState::None);
	}

	constexpr uint64_t GetSocketId() const { return static_cast<uint64_t>(m_socket); }

	std::span<const uint8_t> GetRecvData(int32_t size) const
	{
		const uint8_t* begin = reinterpret_cast<const uint8_t*>(m_recvOverlappedEx.m_buf);
		return std::span<const uint8_t>(begin, static_cast<size_t>(size));
	}

	void PushPacket(const std::span<const uint8_t> dataSpan)
	{
		if (dataSpan.size() + m_packetDataWritePos > PACKET_DATA_BUFFER_SIZE)
		{
			auto remainDataSize = m_packetDataWritePos - m_packetDataReadPos;
			if (remainDataSize > 0)
			{
				CopyMemory(m_packetDataBuffer, &m_packetDataBuffer[m_packetDataReadPos], remainDataSize);
				m_packetDataWritePos = remainDataSize;
			}
			else
			{
				m_packetDataWritePos = 0;
			}
			m_packetDataReadPos = 0;
		}
		CopyMemory(&m_packetDataBuffer[m_packetDataWritePos], dataSpan.data(), dataSpan.size());
		m_packetDataWritePos += static_cast<uint32_t>(dataSpan.size());
	}

	std::optional<Packet> PopPacket()
	{
		uint32_t remainByte = m_packetDataWritePos - m_packetDataReadPos;

		if (remainByte < PACKET_HEADER_SIZE)
			return std::nullopt;

		auto pHeader = reinterpret_cast<PacketHeader*>(&m_packetDataBuffer[m_packetDataReadPos]);

		if (pHeader->size > remainByte)
			return std::nullopt;

		Packet packet;
		packet.sessionIndex = m_id;
		packet.packetId     = pHeader->id;
		const uint8_t* begin = reinterpret_cast<const uint8_t*>(m_packetDataBuffer + m_packetDataReadPos);
		packet.dataSpan      = std::span<const uint8_t>(begin, static_cast<size_t>(pHeader->size));

		m_packetDataReadPos += pHeader->size;
		return packet;
	}

	virtual bool RegisterSocket(HANDLE iocpHandle, SOCKET socket)
	{
		m_socket    = socket;
		m_isConnect = true;

		if (!RegisterIOCP(iocpHandle))
		{
			Log::Error("IOCP bind failed (Id: {})", m_id);
			return false;
		}

		BindRecv();
		return true;
	}

	virtual void Close(bool isForce = false)
	{
		linger lingerOpt{ 0, 0 };
		if (isForce) lingerOpt.l_onoff = 1;

		shutdown(m_socket, SD_BOTH);
		setsockopt(m_socket, SOL_SOCKET, SO_LINGER, (char*)&lingerOpt, sizeof(lingerOpt));

		m_isConnect = false;
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;

		InitClear();
	}

	bool BindAccept(SOCKET listenSock, OverlappedEx& acceptOver)
	{
		m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (m_socket == INVALID_SOCKET)
		{
			Log::ErrorDisplay("client WSASocket() failed", GetLastError());
			return false;
		}

		ZeroMemory(&acceptOver, sizeof(OVERLAPPED));
		acceptOver.m_operation = IOOperation::ACCEPT;

		DWORD bytes = 0;
		if (!AcceptEx(listenSock, m_socket, acceptOver.m_buf, 0,
			sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
			&bytes, reinterpret_cast<LPWSAOVERLAPPED>(&acceptOver)))
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				Log::ErrorDisplay("AcceptEx failed", WSAGetLastError());
				return false;
			}
		}
		return true;
	}

	bool AcceptCompleted(HANDLE iocpHandle)
	{
		Log::Info("AcceptCompleted : SessionId({})", m_id);
		return RegisterSocket(iocpHandle, m_socket);
	}

	bool BindRecv()
	{
		DWORD dwFlag         = 0;
		DWORD dwRecvNumBytes = 0;

		ZeroMemory(&m_recvOverlappedEx, sizeof(OverlappedEx));
		m_recvOverlappedEx.m_wsa.len   = MAX_SOCKBUF;
		m_recvOverlappedEx.m_wsa.buf   = m_recvOverlappedEx.m_buf;
		m_recvOverlappedEx.m_operation = IOOperation::RECV;

		int ret = WSARecv(m_socket, &m_recvOverlappedEx.m_wsa, 1,
			&dwRecvNumBytes, &dwFlag,
			(LPWSAOVERLAPPED)&m_recvOverlappedEx, nullptr);

		if (ret == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
		{
			Log::ErrorDisplay("WSARecv() failed", WSAGetLastError());
			return false;
		}
		return true;
	}

	bool Send(const std::span<const uint8_t> msg) const
	{
		OverlappedEx* sendOverlappedEx = new OverlappedEx();
		sendOverlappedEx->m_wsa.len    = static_cast<uint32_t>(msg.size());
		sendOverlappedEx->m_wsa.buf    = sendOverlappedEx->m_buf;
		sendOverlappedEx->m_operation  = IOOperation::SEND;
		CopyMemory(sendOverlappedEx->m_buf, msg.data(), msg.size());

		DWORD dwSentBytes = 0;
		int ret = WSASend(m_socket, &sendOverlappedEx->m_wsa, 1,
			&dwSentBytes, 0,
			reinterpret_cast<LPWSAOVERLAPPED>(sendOverlappedEx), nullptr);

		if (ret == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
		{
			Log::ErrorDisplay("WSASend() failed", WSAGetLastError());
			delete sendOverlappedEx;
			return false;
		}
		return true;
	}

	void SendCompleted(const uint32_t /*dataSize*/, OverlappedEx* sendOverlappedEx)
	{
		delete sendOverlappedEx;
	}

	virtual void Dispatch(const Packet& packet) {}

private:
	bool RegisterIOCP(HANDLE iocpHandle) const
	{
		auto hIOCP = CreateIoCompletionPort((HANDLE)m_socket, iocpHandle, (ULONG_PTR)this, 0);
		if (hIOCP == nullptr || iocpHandle != hIOCP)
		{
			Log::ErrorDisplay("CreateIoCompletionPort() failed", GetLastError());
			return false;
		}
		return true;
	}

	bool   m_isConnect = false;
	SOCKET m_socket    = INVALID_SOCKET;

	OverlappedEx m_recvOverlappedEx{};

	uint8_t* m_packetDataBuffer   = nullptr;
	uint32_t m_packetDataWritePos = 0;
	uint32_t m_packetDataReadPos  = 0;
};
