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
		// Compact: 미처리 데이터를 앞으로 당긴다
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

	if (dataSpan.size() + m_packetDataWritePos > PACKET_DATA_BUFFER_SIZE)
	{
		Log::Error("PushPacket overflow: writePos={} incoming={} - dropping",
			m_packetDataWritePos, dataSpan.size());
		return;
	}

	CopyMemory(&m_packetDataBuffer[m_packetDataWritePos], dataSpan.data(), dataSpan.size());
	m_packetDataWritePos += static_cast<uint32_t>(dataSpan.size());
}

// 와이어 헤더: [0]=전체 길이(1바이트), [1..4]=PACKET_TYPE(4바이트)
std::optional<Packet> Session::PopPacket()
{
	uint32_t remainByte = m_packetDataWritePos - m_packetDataReadPos;

	if (remainByte < PACKET_LEN_FIELD_SIZE)
		return std::nullopt;

	uint32_t pktSize = *reinterpret_cast<const uint16_t*>(&m_packetDataBuffer[m_packetDataReadPos]);   // size(2바이트)
	if (pktSize == 0)
	{
		// 손상된 길이: 더 진행 못 함 버퍼를 비워 무한루프 방지
		Log::Error("PopPacket: zero-length packet, flushing buffer");
		m_packetDataReadPos = m_packetDataWritePos = 0;
		return std::nullopt;
	}

	if (pktSize > remainByte)
		return std::nullopt;   // 아직 전체 도착 안 함

	Packet packet;
	packet.sessionIndex = m_slotIndex;
	packet.packetId =
		(pktSize >= PACKET_TYPE_OFFSET + sizeof(uint16_t))
		? *reinterpret_cast<const uint16_t*>(&m_packetDataBuffer[m_packetDataReadPos + PACKET_TYPE_OFFSET])
		: 0;
	const uint8_t* begin = m_packetDataBuffer + m_packetDataReadPos;
	packet.dataSpan      = std::span<const uint8_t>(begin, static_cast<size_t>(pktSize));

	m_packetDataReadPos += pktSize;
	return packet;
}

bool Session::RegisterSocket(HANDLE iocpHandle, SOCKET socket, uint32_t slotIndex)
{
	// 초기 풀 세션은 Init() 을 거치지 않으므로 여기서도 버퍼를 보장한다
	if (m_packetDataBuffer == nullptr)
		m_packetDataBuffer = new uint8_t[PACKET_DATA_BUFFER_SIZE];

	m_packetDataReadPos  = 0;
	m_packetDataWritePos = 0;

	m_socket       = socket;
	m_slotIndex    = slotIndex;
	m_id           = s_nextId.fetch_add(1);
	m_sessionState = SessionState::Connected;

	// 네이글 비활성화: 소형 패킷이 지연 ACK와 맞물려 RTT가 늘어나는 것을 막는다
	BOOL noDelay = TRUE;
	setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, sizeof(noDelay));

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
		Log::ErrorDisplay("client WSASocket() 실패", GetLastError());
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
			Log::ErrorDisplay("AcceptEx 실패", WSAGetLastError());
			return false;
		}
	}
	return true;
}

bool Session::AcceptCompleted(HANDLE iocpHandle, uint32_t slotIndex)
{
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
		Log::ErrorDisplay("WSARecv() 실패", WSAGetLastError());
		return false;
	}
	return true;
}

bool Session::Send(const std::span<const uint8_t> msg)
{
	// 닫히는/닫힌 세션엔 송신 금지(동시 종료 중 브로드캐스트 → WSASend 10038 방지)
	if (m_socket == INVALID_SOCKET || m_sessionState.load() == SessionState::Retiring)
		return false;

	if (msg.size() > MAX_SOCKBUF)
	{
		Log::Error("패킷 크기 초과 ({})", msg.size());
		return false;
	}

	OverlappedEx* over = g_sendPool.Acquire();   // 고갈 시 heap 폴백(null 반환 없음)

	CopyMemory(over->m_buf, msg.data(), msg.size());
	over->m_wsa.buf   = over->m_buf;
	over->m_wsa.len   = static_cast<uint32_t>(msg.size());
	over->m_operation = IOOperation::SEND;

	// 큐 push 와 등록 플래그 전이는 반드시 같은 락 안에서.
	// 나누면 SendCompleted 가 큐를 비운 뒤 플래그를 내리기 직전에 push 가 끼어들어
	// 아무도 BindSend 를 부르지 않는 상태로 패킷이 큐에 갇힌다.
	bool needBind = false;
	{
		std::lock_guard lock(m_sendMutex);
		m_sendQueue.push(over);
		if (!m_sendRegistered)
		{
			m_sendRegistered = true;
			needBind = true;
		}
	}

	if (needBind)
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
		Log::ErrorDisplay("WSASend() 실패", WSAGetLastError());
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
		if (!hasMore)
			m_sendRegistered = false;   // 큐 비움과 같은 락 안에서 내려야 push 와 어긋나지 않는다
	}
	g_sendPool.Release(over);

	if (hasMore)
		BindSend();
}

bool Session::RegisterIOCP(HANDLE iocpHandle, uint32_t slotIndex) const
{
	auto hIOCP = CreateIoCompletionPort((HANDLE)m_socket, iocpHandle, (ULONG_PTR)slotIndex, 0);
	if (hIOCP == nullptr || iocpHandle != hIOCP)
	{
		Log::ErrorDisplay("CreateIoCompletionPort() 실패", GetLastError());
		return false;
	}
	return true;
}
