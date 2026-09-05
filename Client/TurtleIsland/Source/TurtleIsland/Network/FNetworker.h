c#pragma once
#include "CoreMinimal.h"
#include "Sockets.h"
#include <atomic>

class FNetworker : public FRunnable
{
public:
	explicit FNetworker(FString InServerIP = TEXT("127.0.0.1"));
	virtual ~FNetworker() override;

	void EnqueSendPacket(TArray<uint8>&& packet);
	TArray<uint8> DequeSendPacket();

	void EnqueRecvPacket(TArray<uint8>&& packet);
	TArray<uint8> DequeRecvPacket();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Exit() override;
	virtual void Stop() override;


	bool IsConnected() const { return bConnected.load(); }

private:
	void ProcessRecvBuffer();

	FString ServerAddress;
	FSocket* Socket = nullptr;
	bool IsRunning = false;
	std::atomic<bool> bConnected{false};
	TQueue<TArray<uint8>, EQueueMode::Spsc> SendQueue;
	TQueue<TArray<uint8>, EQueueMode::Spsc> RecvQueue;

	// Stream reassembly buffer
	TArray<uint8> RecvStreamBuf;
};
