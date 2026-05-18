// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/NetSubsystem.h"
#include "NetSettings.h"

#include <Protocol.h>

#include "FNetworker.h"
#include "Kismet/GameplayStatics.h"

void UNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	Networker = new FNetworker();
	Thread = FRunnableThread::Create(Networker, TEXT("Network Thread"));
	
	OpponentClass = UNetSettings::Get()->OpponentClass;

	UE_LOG(LogTemp, Warning, TEXT("Network Thread Started!"));
}

void UNetSubsystem::Deinitialize()
{
	if (Networker)
	{
		Networker->Stop();
	}
	if (Thread)
	{
		Thread->Kill(true);
		
		delete Thread;
		Thread = nullptr;
	}
	
	if (Networker)
	{
		delete Networker;
		Networker = nullptr;
	}
	Super::Deinitialize();
}

void UNetSubsystem::Tick(float DeltaTime)
{
	if (Networker == nullptr) return;

	TArray<uint8> RecvData = Networker->DequeRecvPacket();
	while (RecvData.Num() > 0)
	{
		if (RecvData.Num() < PACKET_HEADER_SIZE) continue;
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(RecvData.GetData());
		PACKET_ID Id = static_cast<PACKET_ID>(Header->id);

		switch (Id)
		{
		case PACKET_ID::SC_LOGIN_RESPONSE:
			{
				SC_Login* Pkt = reinterpret_cast<SC_Login*>(RecvData.GetData());

				if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
				{
					UE_LOG(LogTemp, Log, TEXT("[Net] 로그인 성공 (playerId=%d)"), Pkt->playerId);
					myId = Pkt->playerId;

					// 로비 입장 요청
					CS_LobbyEnter LobbyPkt{};
					LobbyPkt.size = CS_LOBBY_ENTER_PACKET_SIZE;
					LobbyPkt.id   = static_cast<uint16>(PACKET_ID::CS_LOBBY_ENTER_REQUEST);
					TArray<uint8> Buf;
					Buf.SetNumUninitialized(CS_LOBBY_ENTER_PACKET_SIZE);
					FMemory::Memcpy(Buf.GetData(), &LobbyPkt, CS_LOBBY_ENTER_PACKET_SIZE);
					EnqueSendPacket(MoveTemp(Buf));

					UGameplayStatics::OpenLevel(GetWorld(), FName("Lv1_Lobby"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[Net] 로그인 실패 (result=%d)"), Pkt->result);
				}
			break;
			}
		case PACKET_ID::SC_LOBBY_RESPONSE:
			{
				// 가변 길이 패킷: PacketHeader + int16 roomCount + RoomInfo[]
				if (RecvData.Num() < (int32)(sizeof(PacketHeader) + sizeof(int16)))
					break;

				int16 RoomCount = *reinterpret_cast<int16*>(RecvData.GetData() + sizeof(PacketHeader));
				UE_LOG(LogTemp, Log, TEXT("[Net] 로비 응답 수신 - 방 개수: %d"), RoomCount);

				RoomList.Empty();
				const uint8* RoomPtr = RecvData.GetData() + sizeof(PacketHeader) + sizeof(int16);
				for (int16 i = 0; i < RoomCount; ++i)
				{
					if (RoomPtr + sizeof(RoomInfo) > RecvData.GetData() + RecvData.Num()) break;

					const RoomInfo* Info = reinterpret_cast<const RoomInfo*>(RoomPtr);
					FRoomData Data;
					Data.RoomId    = Info->roomId;
					Data.Title     = UTF8_TO_TCHAR(Info->roomTitle);
					Data.HostName  = UTF8_TO_TCHAR(Info->hostName);
					Data.UserCount = Info->userCount;
					RoomList.Add(Data);

					UE_LOG(LogTemp, Log, TEXT("  [%d] %s (host: %s, %d명)"),
						Data.RoomId, *Data.Title, *Data.HostName, Data.UserCount);

					RoomPtr += sizeof(RoomInfo);
				}
			break;
			}
		case PACKET_ID::SC_ROOM_ENTER_RESPONSE:
			{
				SC_RoomEnter* Pkt = reinterpret_cast<SC_RoomEnter*>(RecvData.GetData());
				if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
				{
					UE_LOG(LogTemp, Log, TEXT("[Net] 방 입장 성공"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[Net] 방 입장 실패 (result=%d)"), Pkt->result);
				}
			break;
			}
		case PACKET_ID::SC_MOVE_NOTIFY:
			{
				SC_MoveNotify* Pkt = reinterpret_cast<SC_MoveNotify*>(RecvData.GetData());

				// 서버는 보낸 사람에게 SC_MOVE_NOTIFY를 보내지 않으므로
				// 수신되는 건 항상 다른 플레이어의 움직임
				if (players.Contains(Pkt->userIndex))
				{
					AOpponentCharacter* Opponent = players[Pkt->userIndex];
					if (Opponent)
					{
						FVector NewLocation(Pkt->x, Pkt->y, Pkt->z);
						FRotator NewRotation(0.f, Pkt->yaw, 0.f);
						Opponent->SetActorLocationAndRotation(NewLocation, NewRotation);
					}
				}
				else
				{
					// 처음 보는 플레이어 → 스폰
					if (OpponentClass && GetWorld())
					{
						FVector SpawnPos(Pkt->x, Pkt->y, Pkt->z);
						AOpponentCharacter* NewOpponent = GetWorld()->SpawnActor<AOpponentCharacter>(
							OpponentClass, SpawnPos, FRotator(0.f, Pkt->yaw, 0.f));
						if (NewOpponent)
						{
							players.Emplace(Pkt->userIndex, NewOpponent);
							UE_LOG(LogTemp, Log, TEXT("[Net] 상대방 스폰 (userId=%d)"), Pkt->userIndex);
						}
					}
				}
			break;
			}
		case PACKET_ID::SC_ROOM_START_NOTIFY:
			{
				SC_RoomStart* Pkt = reinterpret_cast<SC_RoomStart*>(RecvData.GetData());
				if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
				{
					// Ingame
					UGameplayStatics::OpenLevel(GetWorld(), FName("Lv3_InGame"));

				}
				UE_LOG(LogTemp, Log, TEXT("Ingame 패킷 수신"));
			}
			break;
		}
		RecvData = Networker->DequeRecvPacket();
	}

}

TStatId UNetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNetSubsystem, STATGROUP_Tickables);
}

void UNetSubsystem::EnqueSendPacket(TArray<uint8>&& packet)
{
	Networker->EnqueSendPacket(MoveTemp(packet));
}

void UNetSubsystem::EnterRoom(int32 RoomIndex)
{
	CS_RoomEnter Pkt{};
	Pkt.size      = CS_ROOM_ENTER_PACKET_SIZE;
	Pkt.id        = static_cast<uint16>(PACKET_ID::CS_ROOM_ENTER_REQUEST);
	Pkt.roomIndex = static_cast<uint32>(RoomIndex);

	TArray<uint8> Buf;
	Buf.SetNumUninitialized(CS_ROOM_ENTER_PACKET_SIZE);
	FMemory::Memcpy(Buf.GetData(), &Pkt, CS_ROOM_ENTER_PACKET_SIZE);
	EnqueSendPacket(MoveTemp(Buf));

	UE_LOG(LogTemp, Log, TEXT("[Net] 방 입장 요청 (roomIndex=%d)"), RoomIndex);
}

void UNetSubsystem::CopyStringToBuffer(char* Dest, int32 DestSize, const FString& Source)
{
	FTCHARToUTF8 Converter(*Source);
	int32 CopySize = FMath::Min(Converter.Length(), DestSize - 1);
	FMemory::Memcpy(Dest, Converter.Get(), CopySize);
	Dest[CopySize] = '\0'; // 안전하게 마지막 널 문자 삽입
}
