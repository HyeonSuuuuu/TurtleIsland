#include "pch.h"
#include "IOCPNetwork.h"

IOCPNetwork::~IOCPNetwork()
{
	WSACleanup();
}

bool IOCPNetwork::InitSocket()
{
	WSADATA wsaData;
	int32_t ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret != 0)
	{
		Log::ErrorDisplay("WSAStartup() 실패", WSAGetLastError());
		return false;
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		Log::ErrorDisplay("Winsock.dll의 사용 가능한 버전을 찾을 수 없습니다", WSAGetLastError());
		return false;
	}

	m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (m_listenSocket == INVALID_SOCKET)
	{
		Log::ErrorDisplay("socket() 실패", WSAGetLastError());
		return false;
	}

	Log::Success("소켓 초기화 성공");
	return true;
}

bool IOCPNetwork::ConnectToAIServer(const std::string& ip, uint16_t port)
{
	m_aiSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (m_aiSocket == INVALID_SOCKET)
	{
		Log::Error("Ai 소켓 생성 실패", WSAGetLastError());
		return false;
	}

	sockaddr_in aiAddr{};
	aiAddr.sin_family = AF_INET;
	inet_pton(AF_INET, ip.c_str(), &aiAddr.sin_addr);
	aiAddr.sin_port = htons(port);

	Log::Info("Ai 서버에 연결 시도 (IP: {}, Port: {})", ip, port);
	if (connect(m_aiSocket, (sockaddr*)&aiAddr, sizeof(aiAddr)) == SOCKET_ERROR)
	{
		Log::Error("Ai 서버 연결 실패", WSAGetLastError());
		closesocket(m_aiSocket);
		return false;
	}

	m_aiSession = std::make_unique<Session>();
	m_aiSession->Init(AI_ID);

	HANDLE hIOCP = CreateIoCompletionPort((HANDLE)m_aiSocket, m_iocpHandle, (ULONG_PTR)AI_ID, 0);
	if (hIOCP == nullptr || hIOCP != m_iocpHandle)
	{
		Log::ErrorDisplay("aiSocket IOCP 등록 실패", GetLastError());
		return false;
	}

	return true;
}

bool IOCPNetwork::Listen(const int32_t port) const
{
	sockaddr_in serverAddr{};
	serverAddr.sin_family      = AF_INET;
	serverAddr.sin_port        = htons(port);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(m_listenSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR_IN)) != 0)
	{
		Log::ErrorDisplay("bind() 실패", WSAGetLastError());
		return false;
	}

	if (listen(m_listenSocket, SOMAXCONN) != 0)
	{
		Log::ErrorDisplay("listen() 실패", WSAGetLastError());
		return false;
	}

	Log::Success("서버 등록 성공");
	return true;
}

bool IOCPNetwork::StartServer()
{
	g_userPool.Init();
	for (uint32_t i = 0; i < NetConfig::MAX_CLIENTS; ++i)
		g_objects[i] = g_userPool.Get(i);

	m_maxWorkerThread = std::thread::hardware_concurrency() * 2;
	m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, m_maxWorkerThread);
	if (m_iocpHandle == nullptr)
	{
		Log::ErrorDisplay("CreateIoCompletionPort() 실패", GetLastError());
		return false;
	}

	HANDLE hIOCP = CreateIoCompletionPort((HANDLE)m_listenSocket, m_iocpHandle, (ULONG_PTR)0, 0);
	if (hIOCP == nullptr || hIOCP != m_iocpHandle)
	{
		Log::ErrorDisplay("ListenSocket IOCP 등록 실패", GetLastError());
		return false;
	}

	if (!CreateWorkerThread())
		return false;

	for (uint32_t i = 0; i < ACCEPT_COUNT; ++i)
	{
		uint32_t idx;
		if (!g_userPool.Acquire(idx))
		{
			Log::Error("초기 세션 부족");
			break;
		}
		if (!g_userPool.Get(idx)->BindAccept(m_listenSocket, m_acceptOverlappeds[i], idx))
			g_userPool.Release(idx);
	}

	Log::Success("서버 시작");
	return true;
}

void IOCPNetwork::Stop()
{
	m_isWorkerRun = false;
	CloseHandle(m_iocpHandle);

	for (auto& th : m_IOWorkerThreads)
	{
		if (th.joinable()) th.join();
	}

	closesocket(m_listenSocket);
}

User* IOCPNetwork::GetUser(uint32_t index)
{
	if (index >= NetConfig::MAX_CLIENTS) return nullptr;
	return g_userPool.Get(index);
}

bool IOCPNetwork::CreateWorkerThread()
{
	m_isWorkerRun = true;
	for (uint32_t i = 0; i < m_maxWorkerThread; ++i)
	{
		m_IOWorkerThreads.emplace_back([this]() { WorkerThread(); });
	}
	Log::Success("{}개의 Worker Thread 생성 완료", m_maxWorkerThread);
	return true;
}

void IOCPNetwork::WorkerThread()
{
	EBR::RegisterThread();

	uint32_t     slotIdx      = INVALID_SESSION_INDEX;
	bool         isSuccess    = true;
	DWORD        dwIoSize     = 0;
	LPOVERLAPPED lpOverlapped = nullptr;

	while (m_isWorkerRun)
	{
		EBR::Start();

		isSuccess = GetQueuedCompletionStatus(m_iocpHandle,
			&dwIoSize, (PULONG_PTR)&slotIdx, &lpOverlapped, INFINITE);

		if (isSuccess && dwIoSize == 0 && lpOverlapped == nullptr)
		{
			m_isWorkerRun = false;
			EBR::End();
			continue;
		}

		if (lpOverlapped == nullptr)
		{
			Log::ErrorDisplay("GetQueuedCompletionStatus() 실패", GetLastError());
			EBR::End();
			continue;
		}

		OverlappedEx* pOverlappedEx = (OverlappedEx*)lpOverlapped;

		if (IOOperation::ACCEPT == pOverlappedEx->m_operation)
		{
			uint32_t acceptedSlot = pOverlappedEx->m_slotIndex;
			User* pUser = g_userPool.Get(acceptedSlot);
			if (pUser)
			{
				pUser->AcceptCompleted(m_iocpHandle, acceptedSlot);
				g_idToIndexMap[pUser->GetId()] = acceptedSlot;
				OnConnect(acceptedSlot);
				m_clientCnt.fetch_add(1);

				uint32_t newIdx;
				if (!g_userPool.Acquire(newIdx))
					Log::Error("초기 세션 부족");
				else if (!g_userPool.Get(newIdx)->BindAccept(m_listenSocket, *pOverlappedEx, newIdx))
					g_userPool.Retire(newIdx);
			}
		}
		else if (slotIdx == AI_ID)
		{
			if (!isSuccess || dwIoSize == 0)
			{
				Log::Warning("AI 서버 연결 끊김");
			}
			else if (IOOperation::RECV == pOverlappedEx->m_operation)
			{
				const auto dataSpan = m_aiSession->GetRecvData(dwIoSize);
				m_aiSession->PushPacket(dataSpan);
				while (auto packet = m_aiSession->PopPacket())
					OnRecv(AI_ID, packet.value());
				m_aiSession->BindRecv();
			}
			else if (IOOperation::SEND == pOverlappedEx->m_operation)
			{
				m_aiSession->SendCompleted(dwIoSize, pOverlappedEx);
			}
		}
		else if (!isSuccess || (dwIoSize == 0 && isSuccess))
		{
			if (slotIdx < NetConfig::MAX_CLIENTS) Disconnect(slotIdx);
		}
		else if (IOOperation::RECV == pOverlappedEx->m_operation)
		{
			User* pUser = g_userPool.Get(slotIdx);
			if (pUser)
			{
				const auto dataSpan = pUser->GetRecvData(dwIoSize);
				pUser->PushPacket(dataSpan);
				while (auto packet = pUser->PopPacket())
					OnRecv(slotIdx, packet.value());
				pUser->BindRecv();
			}
		}
		else if (IOOperation::SEND == pOverlappedEx->m_operation)
		{
			User* pUser = g_userPool.Get(slotIdx);
			if (pUser)
			{
				pUser->SendCompleted(dwIoSize, pOverlappedEx);
				OnSend(slotIdx);
			}
		}
		else
		{
			Log::Warning("socket({})에서 예외상황", slotIdx);
		}

		EBR::End();
	}

	EBR::UnregisterThread();
}

void IOCPNetwork::Disconnect(uint32_t slotIndex, bool isForce)
{
	User* pUser = g_userPool.Get(slotIndex);
	g_idToIndexMap.erase(pUser->GetId());
	pUser->Close(isForce);
	OnClose(slotIndex);
	g_userPool.Retire(slotIndex);
	m_clientCnt.fetch_sub(1);
}
