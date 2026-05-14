module;
#define WIN32_LEAN_AND_MEAN
#include <winSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

export module IOCPNetwork;
import Common;
import Define;
import User;
import Globals;
import "../../Common/Protocol.h";

export inline std::array<User, NetConfig::MAX_CLIENTS> g_users{};

export class IOCPNetwork
{
public:
	IOCPNetwork() = default;
	virtual ~IOCPNetwork()
	{
		WSACleanup();
	}

	bool InitSocket()
	{
		WSADATA wsaData;
		int32 ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
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

	bool ConnectToAIServer(const std::string& ip, uint16 port)
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

		HANDLE hIOCP = CreateIoCompletionPort((HANDLE)m_aiSocket, m_iocpHandle, (ULONG_PTR)m_aiSession.get(), 0);
		if (hIOCP == nullptr || hIOCP != m_iocpHandle)
		{
			Log::ErrorDisplay("aiSocket IOCP 등록 실패", GetLastError());
			return false;
		}

		return true;
	}

	bool Listen(const int32 port) const
	{
		sockaddr_in serverAddr{};
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(port);
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

	bool StartServer()
	{
		InitSessions();
		// 일단 2배 (밴치마크 후 적절히 조정)
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

		for (uint32 i = 0; i < ACCEPT_COUNT; ++i)
		{
			Session* session = AcquireSession();
			if (session == nullptr)
			{
				Log::Error("초기 세션 부족");
				break;
			}
			if (!session->BindAccept(m_listenSocket, m_acceptOverlappeds[i]))
				ReleaseSession(session->GetId());
		}

		Log::Success("서버 시작");
		return true;
	}

	void Stop()
	{
		m_isWorkerRun = false;
		CloseHandle(m_iocpHandle);

		for (auto& th : m_IOWorkerThreads)
		{
			if (th.joinable()) th.join();
		}

		closesocket(m_listenSocket);
	}

	virtual void OnConnect(const uint32 clientIndex) = 0;
	virtual void OnClose(const uint32 clientIndex) = 0;

	User* GetUser(uint32 index)
	{
		if (index >= NetConfig::MAX_CLIENTS) return nullptr;
		return &g_users[index];
	}

private:
	int64 GetTick()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	Session* AcquireSession()
	{
		std::lock_guard<std::mutex> lock(m_reuseLock);
		if (m_reuseQueue.empty()) return nullptr;
		auto [closedTime, index] = std::move(m_reuseQueue.front());
		if (GetTick() - closedTime < REUSE_WAIT_MS) return nullptr;
		m_reuseQueue.pop();
		return &g_users[index];
	}

	void ReleaseSession(uint32 index)
	{
		std::lock_guard<std::mutex> lock(m_reuseLock);
		m_reuseQueue.emplace(GetTick(), index);
	}

	void InitSessions()
	{
		for (uint32 i = 0; i < NetConfig::MAX_CLIENTS; ++i)
		{
			g_users[i].Init(i);
			m_reuseQueue.emplace(0, i);
		}
	}

	bool CreateWorkerThread()
	{
		m_isWorkerRun = true;
		for (uint32 i = 0; i < m_maxWorkerThread; ++i)
		{
			m_IOWorkerThreads.emplace_back([this]() { WorkerThread(); });
		}
		Log::Success("{}개의 Worker Thread 생성 완료", m_maxWorkerThread);
		return true;
	}

	void WorkerThread()
	{
		Session* pSession = nullptr;
		bool isSuccess = true;
		DWORD dwIoSize = 0;
		LPOVERLAPPED lpOverlapped = nullptr;

		while (m_isWorkerRun)
		{
			isSuccess = GetQueuedCompletionStatus(m_iocpHandle,
				&dwIoSize, (PULONG_PTR)&pSession, &lpOverlapped, INFINITE);

			if (isSuccess && dwIoSize == 0 && lpOverlapped == nullptr)
			{
				m_isWorkerRun = false;
				continue;
			}

			if (lpOverlapped == nullptr)
			{
				Log::ErrorDisplay("GetQueuedCompletionStatus() 실패", GetLastError());
				continue;
			}

			OverlappedEx* pOverlappedEx = (OverlappedEx*)lpOverlapped;

			if (IOOperation::ACCEPT == pOverlappedEx->m_operation)
			{
				User* pUser = static_cast<User*>(pSession);
				if (pUser)
				{
					pUser->AcceptCompleted(m_iocpHandle);
					OnConnect(pUser->GetId());
					m_clientCnt.fetch_add(1);

					Session* session = AcquireSession();
					if (session == nullptr)
						Log::Error("초기 세션 부족");
					else if (!session->BindAccept(m_listenSocket, *pOverlappedEx))
						ReleaseSession(session->GetId());
				}
				continue;
			}
			else if (!isSuccess || (dwIoSize == 0 && isSuccess))
			{
				if (pSession) Disconnect(pSession->GetId());
				continue;
			}
			else if (IOOperation::RECV == pOverlappedEx->m_operation)
			{
				const auto dataSpan = pSession->GetRecvData(dwIoSize);

				User* pUser = &g_users[pSession->GetId()];
				if (pUser)
				{
					pUser->PushPacket(dataSpan);
					while (auto packet = pUser->PopPacket())
						pUser->Dispatch(packet.value());
				}

				pSession->BindRecv();
			}
			else if (IOOperation::SEND == pOverlappedEx->m_operation)
			{
				pSession->SendCompleted(dwIoSize, pOverlappedEx);
			}
			else
			{
				Log::Warning("socket({})에서 예외상황", pSession->GetSocketId());
			}
		}
	}

	void Disconnect(uint32 index, bool isForce = false)
	{
		User* pUser = &g_users[index];
		pUser->Close(isForce);

		OnClose(index);

		ReleaseSession(index);
		m_clientCnt.fetch_sub(1);
	}

	SOCKET m_listenSocket = INVALID_SOCKET;
	SOCKET m_aiSocket = INVALID_SOCKET;
	std::atomic_int m_clientCnt = 0;

	std::vector<std::thread> m_IOWorkerThreads;
	std::atomic_bool m_isWorkerRun = false;
	std::unique_ptr<Session> m_aiSession;

	HANDLE m_iocpHandle = INVALID_HANDLE_VALUE;
	uint32 m_maxWorkerThread = 0;

	std::queue<std::pair<int64, uint32>> m_reuseQueue;
	std::mutex m_reuseLock;

	OverlappedEx m_acceptOverlappeds[ACCEPT_COUNT]{};
};
