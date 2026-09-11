#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include <atomic>

class TURTLEISLAND_API FClientTransport : public FRunnable
{
public:
	explicit FClientTransport(FString InServerIP = TEXT("127.0.0.1"));
	virtual ~FClientTransport() override;

	void EnqueueSendPacket(TArray<uint8>&& Packet);
	TArray<uint8> DequeueRecvPacket();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Exit() override;
	virtual void Stop() override;

	bool IsConnected() const { return bConnected.load(); }
	bool IsConnectionAttemptComplete() const { return bConnectionAttemptComplete.load(); }

private:
	void DrainSendQueue();
	void PumpSocketRecv();
	void ProcessRecvBuffer();
	void CloseSocket();

	FString ServerAddress;
	FSocket* Socket = nullptr;

	std::atomic<bool> bRunning{false};
	std::atomic<bool> bConnected{false};
	std::atomic<bool> bConnectionAttemptComplete{false};

	TQueue<TArray<uint8>, EQueueMode::Spsc> SendQueue;
	TQueue<TArray<uint8>, EQueueMode::Spsc> RecvQueue;

	TArray<uint8> RecvStreamBuf;
	TArray<uint8> PendingSendBuffer;
	int32 PendingSendOffset = 0;
};
