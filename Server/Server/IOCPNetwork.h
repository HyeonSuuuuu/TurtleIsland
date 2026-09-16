#pragma once
#include "pch.h"
#include "Define.h"
#include "Globals.h"   // PooledSession(=User) + g_sessionPool


// =================== IOCPNetwork ====================
// IOCP 골격: 소켓/리슨/워커스레드 + 가상 On* 디스패치
// 콘텐츠(GameServer)가 상속해 On* 를 구현한다 네트워크는 게임을 모른다
class IOCPNetwork
{
public:
	IOCPNetwork() = default;
	virtual ~IOCPNetwork();

	bool InitSocket();
	bool Listen(const int32_t port) const;
	bool StartServer();
	void Stop();

	// AI 서버로 아웃바운드 연결(완료키 = AI_ID). 실패해도 서버는 계속(NpcChat만 비활성).
	// 한 번 주소를 받으면 끊길 때마다 배경에서 다시 붙는다.
	bool ConnectToAIServer(const std::string& ip, uint16_t port);

	virtual void OnConnect(const uint32_t slotIndex) = 0;
	virtual void OnClose(const uint32_t slotIndex)   = 0;
	virtual void OnRecv(uint32_t slotIndex, const Packet& packet) = 0;
	virtual void OnSend(uint32_t slotIndex) {}

	// 콘텐츠가 PostQueuedCompletionStatus 로 게시한 타이머/DB 이벤트 디스패치
	// pOver->m_operation 이 RECV/SEND/ACCEPT 가 아닌 경우 호출된다
	virtual void OnUserEvent(uint32_t completionKey, OverlappedEx* over, DWORD bytes, bool success) {}

	HANDLE GetIocpHandle() const { return m_iocpHandle; }

	PooledSession* GetSession(uint32_t index);

private:
	bool CreateWorkerThread();
	void WorkerThread(int tid);
	void AiReconnectThread();
	void Disconnect(uint32_t index, bool isForce = false);

	SOCKET          m_listenSocket = INVALID_SOCKET;
	std::atomic_int m_clientCnt    = 0;

	std::vector<std::thread> m_IOWorkerThreads;
	std::atomic_bool         m_isWorkerRun = false;

	HANDLE   m_iocpHandle      = INVALID_HANDLE_VALUE;
	uint32_t m_maxWorkerThread = 0;

	OverlappedEx m_acceptOverlappeds[ACCEPT_COUNT]{};

	std::unique_ptr<Session> m_aiSession;   // AI 서버 아웃바운드 세션(완료키 AI_ID)
	// 끊긴 세션은 지우지 않고 보관한다. 워커가 아직 들고 있을 수 있어서, 포인터는 살아 있어야 한다.
	std::vector<std::unique_ptr<Session>> m_retiredAiSessions;
	std::mutex       m_aiMutex;             // 워커의 끊김 처리와 재연결 스레드가 같은 세션을 건드린다
	std::string      m_aiIp;
	uint16_t         m_aiPort = 0;
	std::thread      m_aiReconnectThread;
	std::atomic_bool m_aiReconnectRun = false;
};
