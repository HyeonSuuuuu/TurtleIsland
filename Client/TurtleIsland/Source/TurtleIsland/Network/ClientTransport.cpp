#include "Network/ClientTransport.h"

#include "Protocol.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"

FClientTransport::FClientTransport(FString InServerIP)
	: ServerAddress(MoveTemp(InServerIP))
{
}

FClientTransport::~FClientTransport()
{
	Exit();
}

void FClientTransport::EnqueueSendPacket(TArray<uint8>&& Packet)
{
	if (Packet.Num() > 0)
	{
		SendQueue.Enqueue(MoveTemp(Packet));
	}
}

TArray<uint8> FClientTransport::DequeueRecvPacket()
{
	TArray<uint8> Packet;
	RecvQueue.Dequeue(Packet);
	return Packet;
}

bool FClientTransport::Init()
{
	bRunning.store(true);

	Socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(TEXT("Stream"), TEXT("Client Socket"));
	if (!Socket)
	{
		UE_LOG(LogTemp, Error, TEXT("[Transport] Socket creation failed"));
		bConnectionAttemptComplete.store(true);
		return false;
	}

	FIPv4Address Ip;
	if (!FIPv4Address::Parse(ServerAddress, Ip))
	{
		UE_LOG(LogTemp, Error, TEXT("[Transport] Invalid server address: %s"), *ServerAddress);
		bConnectionAttemptComplete.store(true);
		return false;
	}

	TSharedRef<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	Addr->SetIp(Ip.Value);
	Addr->SetPort(NetConfig::GAME_SERVER_PORT);

	bConnected.store(Socket->Connect(*Addr));
	bConnectionAttemptComplete.store(true);
	return bConnected.load();
}

uint32 FClientTransport::Run()
{
	while (bRunning.load())
	{
		DrainSendQueue();
		PumpSocketRecv();
		FPlatformProcess::Sleep(0.001f);
	}
	return 0;
}

void FClientTransport::Exit()
{
	Stop();
	CloseSocket();
	FRunnable::Exit();
}

void FClientTransport::Stop()
{
	bRunning.store(false);
	FRunnable::Stop();
}

void FClientTransport::DrainSendQueue()
{
	if (!Socket || !bConnected.load())
	{
		return;
	}

	while (bRunning.load())
	{
		if (PendingSendBuffer.Num() == 0)
		{
			SendQueue.Dequeue(PendingSendBuffer);
			PendingSendOffset = 0;
			if (PendingSendBuffer.Num() == 0)
			{
				return;
			}
		}

		int32 BytesSent = 0;
		const int32 Remaining = PendingSendBuffer.Num() - PendingSendOffset;
		if (!Socket->Send(PendingSendBuffer.GetData() + PendingSendOffset, Remaining, BytesSent) || BytesSent <= 0)
		{
			return;
		}

		PendingSendOffset += BytesSent;
		if (PendingSendOffset >= PendingSendBuffer.Num())
		{
			PendingSendBuffer.Reset();
			PendingSendOffset = 0;
		}
	}
}

void FClientTransport::PumpSocketRecv()
{
	if (!Socket || !bConnected.load())
	{
		return;
	}

	constexpr int32 TempBufSize = 65536;
	TArray<uint8> TempBuf;
	TempBuf.SetNumUninitialized(TempBufSize);

	uint32 PendingDataSize = 0;
	while (Socket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
	{
		int32 BytesRead = 0;
		const int32 ToRead = FMath::Min(static_cast<int32>(PendingDataSize), TempBufSize);
		if (Socket->Recv(TempBuf.GetData(), ToRead, BytesRead) && BytesRead > 0)
		{
			RecvStreamBuf.Append(TempBuf.GetData(), BytesRead);
		}
		else
		{
			break;
		}
	}

	ProcessRecvBuffer();
}

void FClientTransport::ProcessRecvBuffer()
{
	while (true)
	{
		if (RecvStreamBuf.Num() < static_cast<int32>(sizeof(PacketHeader)))
		{
			break;
		}

		const PacketHeader* Header = reinterpret_cast<const PacketHeader*>(RecvStreamBuf.GetData());
		const uint16 PacketSize = Header->size;
		if (PacketSize < PACKET_HEADER_SIZE || PacketSize > 1024)
		{
			UE_LOG(LogTemp, Error, TEXT("[Transport] Invalid packet size: %u"), PacketSize);
			RecvStreamBuf.Empty();
			break;
		}

		if (RecvStreamBuf.Num() < static_cast<int32>(PacketSize))
		{
			break;
		}

		TArray<uint8> FullPacket(RecvStreamBuf.GetData(), PacketSize);
		RecvQueue.Enqueue(MoveTemp(FullPacket));
		RecvStreamBuf.RemoveAt(0, PacketSize, EAllowShrinking::No);
	}
}

void FClientTransport::CloseSocket()
{
	bConnected.store(false);
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}
