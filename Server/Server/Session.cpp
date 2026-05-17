#include "pch.h"
#include "Session.h"
#include "Globals.h"

std::atomic<uint32_t> Session::s_nextId{ 1 };

Session::~Session()
{
	delete[] m_packetDataBuffer;
}

void Session::Init(uint32_t id)
{
	GameObject::Init(id);
	m_packetDataBuffer = new uint8_t[PACKET_DATA_BUFFER_SIZE];
}

void Session::Clear()
{
	m_packetDataReadPos  = 0;
	m_packetDataWritePos = 0;
	{
		std::lock_guard lock(m_sendMutex);
		while (!m_sendQueue.empty())
		{
			g_sendPool.Release(m_sendQueue.front());
			m_sendQueue.pop();
		}
	}
	m_sendRegistered = false;
	m_id             = INVALID_ID;
	m_sessionState.store(SessionState::None);
}

std::span<const uint8_t> Session::GetRecvData(int32_t size) const
{
	const uint8_t* begin = reinterpret_cast<const uint8_t*>(m_recvOverlappedEx.m_buf);
	return std::span<const uint8_t>(begin, static_cast<size_t>(size));
}

void Session::PushPacket(const std::span<const uint8_t> dataSpan)
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

std::optional<Packet> Session::PopPacket()
{
	uint32_t remainByte = m_packetDataWritePos - m_packetDataReadPos;

	if (remainByte < PACKET_HEADER_SIZE)
		return std::nullopt;

	auto pHeader = reinterpret_cast<PacketHeader*>(&m_packetDataBuffer[m_packetDataReadPos]);

	if (pHeader->size > remainByte)
		return std::nullopt;

	Packet packet;
	packet.sessionIndex      = m_id;
	packet.packetId          = pHeader->id;
	const uint8_t* begin     = reinterpret_cast<const uint8_t*>(m_packetDataBuffer + m_packetDataReadPos);
	packet.dataSpan          = std::span<const uint8_t>(begin, static_cast<size_t>(pHeader->size));

	m_packetDataReadPos += pHeader->size;
	return packet;
}

bool Session::RegisterSocket(HANDLE iocpHandle, SOCKET socket, uint32_t slotIndex)
{
	m_socket       = socket;
	m_id           = s_nextId.fetch_add(1);
	m_sessionState = SessionState::Connected;

	if (!RegisterIOCP(iocpHandle, slotIndex))
	{
		Log::Error("IOCP bind failed (Id: {})", m_id);
		return false;
	}

	BindRecv();
	return true;
}

void Session::Close(bool isForce)
{
	linger lingerOpt{ 0, 0 };
	if (isForce) lingerOpt.l_onoff = 1;

	shutdown(m_socket, SD_BOTH);
	setsockopt(m_socket, SOL_SOCKET, SO_LINGER, (char*)&lingerOpt, sizeof(lingerOpt));

	m_sessionState = SessionState::Retiring;
	closesocket(m_socket);
	m_socket = INVALID_SOCKET;
}

bool Session::BindAccept(SOCKET listenSock, OverlappedEx& acceptOver, uint32_t slotIndex)
{
	m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (m_socket == INVALID_SOCKET)
	{
		Log::ErrorDisplay("client WSASocket() failed", GetLastError());
		return false;
	}

	ZeroMemory(&acceptOver, sizeof(OVERLAPPED));
	acceptOver.m_operation = IOOperation::ACCEPT;
	acceptOver.m_slotIndex = slotIndex;

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

bool Session::AcceptCompleted(HANDLE iocpHandle, uint32_t slotIndex)
{
	Log::Info("AcceptCompleted : SessionId({})", m_id);
	return RegisterSocket(iocpHandle, m_socket, slotIndex);
}

bool Session::BindRecv()
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

bool Session::Send(const std::span<const uint8_t> msg)
{
	if (msg.size() > MAX_SOCKBUF)
	{
		Log::Error("패킷 크기 초과 ({})", msg.size());
		return false;
	}

	OverlappedEx* over = g_sendPool.Acquire();
	if (!over)
	{
		Log::Error("SendPool 고갈");
		return false;
	}

	CopyMemory(over->m_buf, msg.data(), msg.size());
	over->m_wsa.buf   = over->m_buf;
	over->m_wsa.len   = static_cast<uint32_t>(msg.size());
	over->m_operation = IOOperation::SEND;

	{
		std::lock_guard lock(m_sendMutex);
		m_sendQueue.push(over);
	}

	if (!m_sendRegistered.exchange(true))
		BindSend();

	return true;
}

void Session::BindSend()
{
	OverlappedEx* over;
	{
		std::lock_guard lock(m_sendMutex);
		if (m_sendQueue.empty())
		{
			m_sendRegistered = false;
			return;
		}
		over = m_sendQueue.front();
	}

	ZeroMemory(static_cast<OVERLAPPED*>(over), sizeof(OVERLAPPED));

	DWORD dwSentBytes = 0;
	int ret = WSASend(m_socket, &over->m_wsa, 1, &dwSentBytes, 0,
		reinterpret_cast<LPWSAOVERLAPPED>(over), nullptr);

	if (ret == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
	{
		Log::ErrorDisplay("WSASend() failed", WSAGetLastError());
		std::lock_guard lock(m_sendMutex);
		m_sendQueue.pop();
		g_sendPool.Release(over);
		m_sendRegistered = false;
	}
}

void Session::SendCompleted(uint32_t /*bytesSent*/)
{
	OverlappedEx* over;
	bool hasMore;
	{
		std::lock_guard lock(m_sendMutex);
		over = m_sendQueue.front();
		m_sendQueue.pop();
		hasMore = !m_sendQueue.empty();
	}
	g_sendPool.Release(over);

	if (hasMore)
		BindSend();
	else
		m_sendRegistered = false;
}

bool Session::RegisterIOCP(HANDLE iocpHandle, uint32_t slotIndex) const
{
	auto hIOCP = CreateIoCompletionPort((HANDLE)m_socket, iocpHandle, (ULONG_PTR)slotIndex, 0);
	if (hIOCP == nullptr || iocpHandle != hIOCP)
	{
		Log::ErrorDisplay("CreateIoCompletionPort() failed", GetLastError());
		return false;
	}
	return true;
}
