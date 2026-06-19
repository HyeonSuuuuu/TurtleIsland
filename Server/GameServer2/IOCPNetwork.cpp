#include "pch.h"
#include "IOCPNetwork.h"
#include "Globals.h"
#include "Ebr.h"

IOCPNetwork::~IOCPNetwork()
{
	WSACleanup();
}

bool IOCPNetwork::InitSocket()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		Log::ErrorDisplay("WSAStartup() 실패", WSAGetLastError());
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

bool IOCPNetwork::Listen(const int32_t port) const
{
	sockaddr_in serverAddr{};
	serverAddr.sin_family      = AF_INET;
	serverAddr.sin_port        = htons(static_cast<uint16_t>(port));
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

	Log::Success("서버 리슨 성공 (port {})", port);
	return true;
}

bool IOCPNetwork::StartServer()
{
	g_sendPool.Init();

	m_maxWorkerThread = std::thread::hardware_concurrency() * 2;
	if (m_maxWorkerThread == 0) m_maxWorkerThread = 4;
	if (m_maxWorkerThread > MAX_WORKER_THREADS) m_maxWorkerThread = MAX_WORKER_THREADS;

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
			g_userPool.Retire(idx);
	}

	Log::Success("서버 시작");
	return true;
}

void IOCPNetwork::Stop()
{
	m_isWorkerRun = false;
	// 워커들을 깨우기 위해 종료 신호(0,0,nullptr)를 워커 수만큼 게시
	for (uint32_t i = 0; i < m_maxWorkerThread; ++i)
		PostQueuedCompletionStatus(m_iocpHandle, 0, 0, nullptr);

	for (auto& th : m_IOWorkerThreads)
		if (th.joinable()) th.join();

	CloseHandle(m_iocpHandle);
	closesocket(m_listenSocket);
}

PooledSession* IOCPNetwork::GetSession(uint32_t index)
{
	if (index >= MAX_CLIENTS) return nullptr;
	return g_userPool.Get(index);
}

bool IOCPNetwork::ConnectToAIServer(const std::string& ip, uint16_t port)
{
	SOCKET aiSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (aiSock == INVALID_SOCKET)
	{
		Log::Error("[AI] socket 생성 실패");
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);
	inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

	if (WSAConnect(aiSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr), nullptr, nullptr, nullptr, nullptr) == SOCKET_ERROR)
	{
		Log::Warning("[AI] 연결 실패 ({}) — NpcChat 비활성", WSAGetLastError());
		closesocket(aiSock);
		return false;
	}

	// 아웃바운드 세션: 완료키 = AI_ID 로 IOCP 등록 + recv 게시
	m_aiSession = std::make_unique<Session>();
	m_aiSession->Init(AI_ID);
	if (!m_aiSession->RegisterSocket(m_iocpHandle, aiSock, AI_ID))
	{
		Log::Error("[AI] RegisterSocket 실패");
		m_aiSession.reset();
		return false;
	}

	g_aiSession = m_aiSession.get();
	Log::Success("[AI] AI 서버 연결 {}:{}", ip, port);
	return true;
}

bool IOCPNetwork::CreateWorkerThread()
{
	m_isWorkerRun = true;
	for (uint32_t i = 0; i < m_maxWorkerThread; ++i)
		m_IOWorkerThreads.emplace_back([this, i]() { WorkerThread(static_cast<int>(i)); });

	Log::Success("{}개의 Worker Thread 생성 완료", m_maxWorkerThread);
	return true;
}

void IOCPNetwork::WorkerThread(int tid)
{
	ebr::register_thread(tid);

	uint32_t     slotIdx      = INVALID_SESSION_INDEX;
	BOOL         isSuccess    = TRUE;
	DWORD        dwIoSize     = 0;
	LPOVERLAPPED lpOverlapped = nullptr;

	while (m_isWorkerRun)
	{
		ebr::start_op();

		isSuccess = GetQueuedCompletionStatus(m_iocpHandle,
			&dwIoSize, (PULONG_PTR)&slotIdx, &lpOverlapped, INFINITE);

		// 종료 신호(Stop 이 게시한 0,0,nullptr)
		if (isSuccess && dwIoSize == 0 && lpOverlapped == nullptr)
		{
			ebr::end_op();
			break;
		}

		if (lpOverlapped == nullptr)
		{
			Log::ErrorDisplay("GetQueuedCompletionStatus() 실패", GetLastError());
			ebr::end_op();
			continue;
		}

		OverlappedEx* pOver = (OverlappedEx*)lpOverlapped;

		if (IOOperation::ACCEPT == pOver->m_operation)
		{
			uint32_t acceptedSlot = pOver->m_slotIndex;
			PooledSession* pSession = g_userPool.Get(acceptedSlot);
			if (pSession)
			{
				pSession->AcceptCompleted(m_iocpHandle, acceptedSlot);
				g_idToIndexMap[pSession->GetId()] = acceptedSlot;
				OnConnect(acceptedSlot);
				m_clientCnt.fetch_add(1);

				uint32_t newIdx;
				if (!g_userPool.Acquire(newIdx))
					Log::Error("세션 풀 고갈 - accept 재장전 실패");
				else if (!g_userPool.Get(newIdx)->BindAccept(m_listenSocket, *pOver, newIdx))
					g_userPool.Retire(newIdx);
			}
		}
		else if (IOOperation::RECV == pOver->m_operation || IOOperation::SEND == pOver->m_operation)
		{
			// 소켓 완료: 실패/정상종료(0바이트) 는 연결 해제
			if (!isSuccess || dwIoSize == 0)
			{
				if (slotIdx < MAX_CLIENTS) Disconnect(slotIdx);
				// AI 연결 종료는 무시(재연결 미구현)
			}
			else if (slotIdx == AI_ID)
			{
				if (m_aiSession)
				{
					if (IOOperation::RECV == pOver->m_operation)
					{
						const auto dataSpan = m_aiSession->GetRecvData(dwIoSize);
						m_aiSession->PushPacket(dataSpan);
						while (auto packet = m_aiSession->PopPacket())
							OnRecv(AI_ID, packet.value());
						m_aiSession->BindRecv();
					}
					else
					{
						m_aiSession->SendCompleted(dwIoSize);
					}
				}
			}
			else if (IOOperation::RECV == pOver->m_operation)
			{
				PooledSession* pSession = g_userPool.Get(slotIdx);
				if (pSession)
				{
					const auto dataSpan = pSession->GetRecvData(dwIoSize);
					pSession->PushPacket(dataSpan);
					while (auto packet = pSession->PopPacket())
						OnRecv(slotIdx, packet.value());
					pSession->BindRecv();
				}
			}
			else // SEND
			{
				PooledSession* pSession = g_userPool.Get(slotIdx);
				if (pSession)
				{
					pSession->SendCompleted(dwIoSize);
					OnSend(slotIdx);
				}
			}
		}
		else
		{
			// 콘텐츠가 게시한 타이머/DB 이벤트 → 콘텐츠로 디스패치
			OnUserEvent(slotIdx, pOver, dwIoSize, isSuccess != FALSE);
		}

		ebr::end_op();
	}
}

void IOCPNetwork::Disconnect(uint32_t slotIndex, bool isForce)
{
	PooledSession* pSession = g_userPool.Get(slotIndex);
	g_idToIndexMap.erase(pSession->GetId());
	pSession->Close(isForce);
	OnClose(slotIndex);
	g_userPool.Retire(slotIndex);
	m_clientCnt.fetch_sub(1);
}
