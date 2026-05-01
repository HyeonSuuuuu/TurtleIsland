module;
#define WIN32_LEAN_AND_MEAN
#include <winSock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#pragma comment(lib, "mswsock.lib")

export module Session;
import Common;
import Define;
import "../../Common/Protocol.h";

export class Session
{
public:
	static constexpr uint32 PACKET_DATA_BUFFER_SIZE = 8096;

	Session() = default;
	virtual ~Session()
	{
		delete[] m_packetDataBuffer;
	}

	virtual void Init(uint32 id)
	{
		m_index = id;
		m_packetDataBuffer = new byte[PACKET_DATA_BUFFER_SIZE];
	}

	virtual void Clear()
	{
		m_packetDataReadPos = 0;
		m_packetDataWritePos = 0;
	}

	constexpr uint32 GetIndex() const { return m_index; }
	constexpr uint64 GetSocketId() const { return static_cast<uint64>(m_socket); }

	constexpr std::span<const byte> GetRecvData(int32 size) const
	{
		return std::span<const byte>(m_recvBuf, size);
	}

	void PushPacket(const std::span<const byte> dataSpan)
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
		m_packetDataWritePos += static_cast<uint32>(dataSpan.size());
	}

	std::optional<Packet> PopPacket()
	{
		uint32 remainByte = m_packetDataWritePos - m_packetDataReadPos;

		if (remainByte < PACKET_HEADER_SIZE)
			return std::nullopt;

		auto pHeader = (PacketHeader*)&m_packetDataBuffer[m_packetDataReadPos];

		if (pHeader->size > remainByte)
			return std::nullopt;

		Packet packet;
		packet.sessionIndex = m_index;
		packet.packetId = pHeader->id;
		packet.dataSpan = std::span<byte>{ &m_packetDataBuffer[m_packetDataReadPos], pHeader->size };

		m_packetDataReadPos += pHeader->size;
		return packet;
	}

	virtual bool RegisterSocket(HANDLE iocpHandle, SOCKET socket)
	{
		m_socket = socket;
		m_isConnect = true;

		if (!RegisterIOCP(iocpHandle))
		{
			Log::Error("IOCP 바인딩 실패 (Index: {})", m_index);
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

		Clear();
	}

	bool BindAccept(SOCKET listenSock)
	{
		m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (m_socket == INVALID_SOCKET)
		{
			Log::ErrorDisplay("client WSASocket() 실패", GetLastError());
			return false;
		}

		ZeroMemory(&m_acceptOverlappedEx, sizeof(OverlappedEx));
		m_acceptOverlappedEx.m_operation = IOOperation::ACCEPT;

		DWORD bytes = 0;
		if (!AcceptEx(listenSock, m_socket, m_acceptBuf, 0,
			sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
			&bytes, reinterpret_cast<LPWSAOVERLAPPED>(&m_acceptOverlappedEx)))
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				Log::ErrorDisplay("AcceptEx Error", WSAGetLastError());
				return false;
			}
		}
		return true;
	}

	bool AcceptCompleted(HANDLE iocpHandle)
	{
		Log::Info("AcceptCompleted : SessionIndex({})", m_index);
		return RegisterSocket(iocpHandle, m_socket);
	}

	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		ZeroMemory(&m_recvOverlappedEx, sizeof(OverlappedEx));
		m_recvOverlappedEx.m_wsa.len = MAX_SOCKBUF;
		m_recvOverlappedEx.m_wsa.buf = m_recvOverlappedEx.m_buf;
		m_recvOverlappedEx.m_operation = IOOperation::RECV;

		int ret = WSARecv(m_socket, &m_recvOverlappedEx.m_wsa, 1,
			&dwRecvNumBytes, &dwFlag,
			(LPWSAOVERLAPPED)&m_recvOverlappedEx, nullptr);

		if (ret == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
		{
			Log::ErrorDisplay("WSARecv() 실패", WSAGetLastError());
			return false;
		}
		return true;
	}

	bool Send(const std::span<const byte> msg)
	{
		OverlappedEx* sendOverlappedEx = new OverlappedEx();
		sendOverlappedEx->m_wsa.len = static_cast<uint32>(msg.size());
		sendOverlappedEx->m_wsa.buf = sendOverlappedEx->m_buf;
		sendOverlappedEx->m_operation = IOOperation::SEND;
		CopyMemory(sendOverlappedEx->m_buf, msg.data(), msg.size());

		DWORD dwSentBytes = 0;
		int ret = WSASend(m_socket, &sendOverlappedEx->m_wsa, 1,
			&dwSentBytes, 0,
			reinterpret_cast<LPWSAOVERLAPPED>(sendOverlappedEx), nullptr);

		if (ret == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
		{
			Log::ErrorDisplay("WSASend() 실패", WSAGetLastError());
			delete sendOverlappedEx;
			return false;
		}
		return true;
	}

	void SendCompleted(const uint32 dataSize, OverlappedEx* sendOverlappedEx)
	{
		delete sendOverlappedEx;
	}

	virtual void Dispatch(const Packet& packet) {}

protected:
	uint32 m_index = 0;

private:
	bool RegisterIOCP(HANDLE iocpHandle) const
	{
		auto hIOCP = CreateIoCompletionPort((HANDLE)m_socket, iocpHandle, (ULONG_PTR)this, 0);
		if (hIOCP == nullptr || iocpHandle != hIOCP)
		{
			Log::ErrorDisplay("CreateCompletionPort() 실패", GetLastError());
			return false;
		}
		return true;
	}

	bool m_isConnect = false;
	SOCKET m_socket = INVALID_SOCKET;

	OverlappedEx m_acceptOverlappedEx{};
	char m_acceptBuf[64]{};

	OverlappedEx m_recvOverlappedEx{};
	byte m_recvBuf[MAX_SOCKBUF]{};

	byte* m_packetDataBuffer = nullptr;
	uint32 m_packetDataWritePos = 0;
	uint32 m_packetDataReadPos = 0;
};
