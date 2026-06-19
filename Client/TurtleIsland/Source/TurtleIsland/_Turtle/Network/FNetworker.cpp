#include "FNetworker.h"
#include "SocketSubsystem.h"
#include "Protocol.h"
#include "Interfaces/IPv4/IPv4Address.h"
FNetworker::FNetworker(FString InServerIP)
	: ServerAddress(MoveTemp(InServerIP))
{
}

FNetworker::~FNetworker()
{
	Exit();
}

void FNetworker::EnqueSendPacket(TArray<uint8>&& packet)
{
	SendQueue.Enqueue((MoveTemp(packet)));
}

TArray<uint8> FNetworker::DequeSendPacket()
{
	TArray<uint8> PopData;
	SendQueue.Dequeue(PopData);
	return PopData;
}

void FNetworker::EnqueRecvPacket(TArray<uint8>&& packet)
{
	RecvQueue.Enqueue(MoveTemp(packet));
}

TArray<uint8> FNetworker::DequeRecvPacket()
{
	TArray<uint8> PopData;
	RecvQueue.Dequeue(PopData);
	return PopData;
}

bool FNetworker::Init()
{
	IsRunning = true;
	Socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(TEXT("Stream"), TEXT("Client Socket"));
	if (Socket == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Socket Creation Failed"));
		return false;
	}

	FString address = ServerAddress;
	int32 port = NetConfig::GAME_SERVER_PORT;

	FIPv4Address ip;
	FIPv4Address::Parse(address, ip);

	TSharedRef<FInternetAddr> addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	addr->SetIp(ip.Value);
	addr->SetPort(port);


	bConnected = Socket->Connect(*addr);
	return bConnected.load();
}

uint32 FNetworker::Run()
{
	constexpr int32 TempBufSize = 65536;
	TArray<uint8> TempBuf;
	TempBuf.SetNumUninitialized(TempBufSize);

	while (IsRunning)
	{
		// ---- Send ----
		while (true)
		{
			TArray<uint8> SendPacket = DequeSendPacket();
			if (SendPacket.Num() == 0) break;
			if (Socket)
			{
				int32 BytesSent = 0;
				Socket->Send(SendPacket.GetData(), SendPacket.Num(), BytesSent);
			}
		}

		// ---- Recv: 소켓에서 읽어서 스트림 버퍼에 쌓기 ----
		if (Socket)
		{
			uint32 PendingDataSize = 0;
			while (Socket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
			{
				int32 BytesRead = 0;
				int32 ToRead = FMath::Min((int32)PendingDataSize, TempBufSize);
				if (Socket->Recv(TempBuf.GetData(), ToRead, BytesRead) && BytesRead > 0)
				{
					RecvStreamBuf.Append(TempBuf.GetData(), BytesRead);
				}
				else break;
			}

			// 스트림 버퍼에서 완성된 패킷 꺼내기
			ProcessRecvBuffer();
		}

		FPlatformProcess::Sleep(0.001f);
	}
	return 0;
}

void FNetworker::ProcessRecvBuffer()
{
	while (true)
	{
		// 헤더 크기 미만이면 대기
		if (RecvStreamBuf.Num() < (int32)sizeof(PacketHeader)) break;

		PacketHeader* Header = reinterpret_cast<PacketHeader*>(RecvStreamBuf.GetData());
		uint16 PacketSize = Header->size;

		// 잘못된 패킷 크기 방어
		if (PacketSize < (uint16)sizeof(PacketHeader) || PacketSize > 65535)
		{
			UE_LOG(LogTemp, Error, TEXT("[FNetworker] Invalid packet size: %d — clearing buffer"), PacketSize);
			RecvStreamBuf.Empty();
			break;
		}

		// 패킷 전체가 아직 안 왔으면 대기
		if (RecvStreamBuf.Num() < (int32)PacketSize) break;

		// 완성된 패킷 추출
		TArray<uint8> FullPacket(RecvStreamBuf.GetData(), PacketSize);
		EnqueRecvPacket(MoveTemp(FullPacket));

		// 소비한 만큼 버퍼 앞부분 제거
		RecvStreamBuf.RemoveAt(0, PacketSize, EAllowShrinking::No);
	}
}

void FNetworker::Exit()
{
	Stop();
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	FRunnable::Exit();
}

void FNetworker::Stop()
{
	IsRunning = false;
	FRunnable::Stop();
}
