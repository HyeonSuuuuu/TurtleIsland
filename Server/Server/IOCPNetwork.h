#pragma once
#include "pch.h"
#include "Define.h"
#include "User.h"
#include "Globals.h"
#include "../../Common/Protocol.h"

class IOCPNetwork
{
public:
	IOCPNetwork() = default;
	virtual ~IOCPNetwork();

	bool InitSocket();
	bool ConnectToAIServer(const std::string& ip, uint16_t port);
	bool Listen(const int32_t port) const;
	bool StartServer();
	void Stop();

	virtual void OnConnect(const uint32_t clientIndex) = 0;
	virtual void OnClose(const uint32_t clientIndex)   = 0;
	virtual void OnRecv(uint32_t clientIndex, const Packet& packet) = 0;
	virtual void OnSend(uint32_t clientIndex) {}

	User* GetUser(uint32_t index);

private:
	bool CreateWorkerThread();
	void WorkerThread();
	void Disconnect(uint32_t index, bool isForce = false);

	SOCKET           m_listenSocket    = INVALID_SOCKET;
	SOCKET           m_aiSocket        = INVALID_SOCKET;
	std::atomic_int  m_clientCnt       = 0;

	std::vector<std::thread> m_IOWorkerThreads;
	std::atomic_bool         m_isWorkerRun = false;
	std::unique_ptr<Session> m_aiSession;

	HANDLE   m_iocpHandle      = INVALID_HANDLE_VALUE;
	uint32_t m_maxWorkerThread = 0;

	OverlappedEx m_acceptOverlappeds[ACCEPT_COUNT]{};
};
