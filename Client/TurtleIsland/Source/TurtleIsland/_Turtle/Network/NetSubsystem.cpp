// Fill out your copyright notice in the Description page of Project Settings.


#include "NetSubsystem.h"
#include "NetSettings.h"

#include <Protocol.h>

#include "FNetworker.h"
#include "Kismet/GameplayStatics.h"

void UNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Networker = new FNetworker(StoredServerIP);
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
					UE_LOG(LogTemp, Log, TEXT("[Net] 로그인 성공 (playerId=%d, new=%d)"),
						Pkt->playerId, Pkt->isNewAccount);
					myId = Pkt->playerId;

					// 로비 진입은 LoginWidget이 결정(신규 가입이면 안내 팝업 확인 후 진입)
					OnLoginSucceeded.Broadcast(Pkt->isNewAccount != 0);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[Net] 로그인 실패 (result=%d)"), Pkt->result);
					const FString Msg = (Pkt->result == static_cast<uint8>(ERROR_CODE::USER_ALREADY_LOGGED_IN))
						? TEXT("이미 접속 중인 계정입니다.")
						: TEXT("아이디 또는 비밀번호가 올바르지 않습니다.");
					OnLoginFailed.Broadcast(Msg);
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

				OnRoomListUpdated.Broadcast();
			break;
			}
		case PACKET_ID::SC_ROOM_ENTER_RESPONSE:
			{
				SC_RoomEnter* Pkt = reinterpret_cast<SC_RoomEnter*>(RecvData.GetData());
				if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
				{
					UE_LOG(LogTemp, Log, TEXT("[Net] 방 입장 성공"));

					// 참가자 목록을 본인 1명으로 초기화 (다른 참가자는 SC_ROOM_ENTER_NOTIFY로 합류)
					RoomMembers.Reset();
					FRoomMember Self;
					Self.UserID    = StoredUserID;
					Self.UserIndex = static_cast<int32>(myId);
					Self.bIsLocal  = true;
					Self.bIsHost   = (!CurrentRoomHost.IsEmpty() && CurrentRoomHost == StoredUserID);
					RoomMembers.Add(Self);
					OnRoomMembersUpdated.Broadcast();

					UGameplayStatics::OpenLevel(GetWorld(), FName("Lv2_Room"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[Net] 방 입장 실패 (result=%d)"), Pkt->result);
					const FString Msg = (Pkt->result == static_cast<uint8>(ERROR_CODE::ROOM_TITLE_DUP))
						? TEXT("같은 제목의 방이 이미 있습니다.")
						: TEXT("방에 입장할 수 없습니다. (정원 초과 또는 게임 중)");
					OnRoomEnterFailed.Broadcast(Msg);
				}
			break;
			}
		case PACKET_ID::SC_ROOM_ENTER_NOTIFY:
			{
				SC_RoomEnterNotify* Pkt = reinterpret_cast<SC_RoomEnterNotify*>(RecvData.GetData());

				const FString JoinedID = FString(UTF8_TO_TCHAR(Pkt->userID));
				const int32 JoinedIdx  = static_cast<int32>(Pkt->userIndex);

				// 본인/중복은 무시, 정원 초과도 무시
				bool bExists = false;
				for (const FRoomMember& M : RoomMembers)
				{
					if (M.UserIndex == JoinedIdx) { bExists = true; break; }
				}

				if (!bExists && JoinedIdx != static_cast<int32>(myId)
					&& RoomMembers.Num() < MaxRoomMembers)
				{
					FRoomMember Member;
					Member.UserID    = JoinedID;
					Member.UserIndex = JoinedIdx;
					Member.bIsLocal  = false;
					Member.bIsHost   = (!CurrentRoomHost.IsEmpty() && CurrentRoomHost == JoinedID);
					RoomMembers.Add(Member);

					UE_LOG(LogTemp, Log, TEXT("[Net] 방 참가자 합류 (%s, idx=%d)"), *JoinedID, JoinedIdx);
					OnRoomMembersUpdated.Broadcast();
				}
			break;
			}
		case PACKET_ID::SC_MOVE_NOTIFY:
			{
				SC_MoveNotify* Pkt = reinterpret_cast<SC_MoveNotify*>(RecvData.GetData());

				// 서버는 보낸 사람에게 SC_MOVE_NOTIFY를 보내지 않으므로
				// 수신되는 건 항상 다른 플레이어의 움직임
				// 서버는 월드 2D (x, y) 좌표 사용 → Unreal (X, Y) 그대로, 높이(Z)는 바닥 고정
				auto ToUnrealLoc = [GroundZ = OpponentGroundZ](float sx, float sy) -> FVector
				{
					return FVector(sx, sy, GroundZ);
				};

				const FVector NewLocation = ToUnrealLoc(Pkt->x, Pkt->y);
				const FRotator NewRotation(0.f, Pkt->yaw, 0.f);

				if (players.Contains(Pkt->userIndex))
				{
					if (AOpponentCharacter* Opponent = players[Pkt->userIndex])
						Opponent->UpdateNetState(NewLocation, NewRotation);
				}
				else
				{
					// 처음 보는 플레이어 → 스폰
					if (OpponentClass && GetWorld())
					{
						FActorSpawnParameters Params;
						Params.SpawnCollisionHandlingOverride =
							ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
						AOpponentCharacter* NewOpponent = GetWorld()->SpawnActor<AOpponentCharacter>(
							OpponentClass, NewLocation, NewRotation, Params);
						if (NewOpponent)
						{
							NewOpponent->UpdateNetState(NewLocation, NewRotation);
							players.Emplace(Pkt->userIndex, NewOpponent);
							UE_LOG(LogTemp, Log, TEXT("[Net] 상대방 스폰 (userId=%d)"), Pkt->userIndex);
						}
					}
				}
			break;
			}
		case PACKET_ID::SC_ROOM_LEAVE_NOTIFY:
			{
				SC_RoomLeaveNotify* Pkt = reinterpret_cast<SC_RoomLeaveNotify*>(RecvData.GetData());

				AOpponentCharacter** Found = players.Find(Pkt->userIndex);
				if (Found && *Found)
				{
					(*Found)->Destroy();
					players.Remove(Pkt->userIndex);
					UE_LOG(LogTemp, Log, TEXT("[Net] 상대방 퇴장 및 제거 (userId=%d)"), Pkt->userIndex);
				}

				// 방 참가자 목록에서도 제거 후 RoomUI 갱신
				const int32 LeftIdx = static_cast<int32>(Pkt->userIndex);
				const int32 Removed = RoomMembers.RemoveAll(
					[LeftIdx](const FRoomMember& M){ return M.UserIndex == LeftIdx; });
				if (Removed > 0)
					OnRoomMembersUpdated.Broadcast();
			break;
			}
		case PACKET_ID::SC_ROOM_START_NOTIFY:
			{
				SC_RoomStart* Pkt = reinterpret_cast<SC_RoomStart*>(RecvData.GetData());
				if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
				{
					// Ingame (맵 파일명과 대소문자 일치 필요: Lv3_Ingame)
					UGameplayStatics::OpenLevel(GetWorld(), FName("Lv3_Ingame"));

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

void UNetSubsystem::NotifyIngameReady(float X, float Y, float Yaw)
{
	CS_IngameReady Pkt{};
	Pkt.size = CS_INGAME_READY_PACKET_SIZE;
	Pkt.id   = static_cast<uint16>(PACKET_ID::CS_INGAME_READY);
	Pkt.x    = X;
	Pkt.y    = Y;
	Pkt.yaw  = Yaw;

	TArray<uint8> Buf;
	Buf.SetNumUninitialized(CS_INGAME_READY_PACKET_SIZE);
	FMemory::Memcpy(Buf.GetData(), &Pkt, CS_INGAME_READY_PACKET_SIZE);
	EnqueSendPacket(MoveTemp(Buf));

	UE_LOG(LogTemp, Log, TEXT("[Net] 인게임 준비 완료 통지"));
}

void UNetSubsystem::EnterLobby()
{
	// CS_LOBBY_ENTER 전송 후 로비 레벨 로드
	CS_LobbyEnter LobbyPkt{};
	LobbyPkt.size = CS_LOBBY_ENTER_PACKET_SIZE;
	LobbyPkt.id   = static_cast<uint16>(PACKET_ID::CS_LOBBY_ENTER_REQUEST);

	TArray<uint8> Buf;
	Buf.SetNumUninitialized(CS_LOBBY_ENTER_PACKET_SIZE);
	FMemory::Memcpy(Buf.GetData(), &LobbyPkt, CS_LOBBY_ENTER_PACKET_SIZE);
	EnqueSendPacket(MoveTemp(Buf));

	UGameplayStatics::OpenLevel(GetWorld(), FName("Lv1_Lobby"));
}

void UNetSubsystem::EnterRoom(int32 RoomIndex)
{
	// 입장 응답이 오기 전에, 로비 방 목록에서 제목/방장을 미리 캐시해 둔다
	CurrentRoomTitle.Empty();
	CurrentRoomHost.Empty();
	for (const FRoomData& Room : RoomList)
	{
		if (Room.RoomId == RoomIndex)
		{
			CurrentRoomTitle = Room.Title;
			CurrentRoomHost  = Room.HostName;
			break;
		}
	}

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

void UNetSubsystem::CreateRoom(const FString& Title)
{
	// 생성 직후 서버가 바로 입장시키므로(SC_ROOM_ENTER_RESPONSE) 제목/방장을
	// 미리 캐시해 둔다. 캐시하지 않으면 RoomUI가 빈 제목("대기실")으로 뜬다.
	CurrentRoomTitle = Title;
	CurrentRoomHost  = StoredUserID;   // 생성자가 곧 방장

	CS_RoomCreate Pkt{};
	Pkt.size = CS_ROOM_CREATE_PACKET_SIZE;
	Pkt.id   = static_cast<uint16>(PACKET_ID::CS_ROOM_CREATE_REQUEST);
	CopyStringToBuffer(Pkt.roomTitle, MAX_ROOM_TITLE_LEN, Title);

	TArray<uint8> Buf;
	Buf.SetNumUninitialized(CS_ROOM_CREATE_PACKET_SIZE);
	FMemory::Memcpy(Buf.GetData(), &Pkt, CS_ROOM_CREATE_PACKET_SIZE);
	EnqueSendPacket(MoveTemp(Buf));

	UE_LOG(LogTemp, Log, TEXT("[Net] 방 생성 요청 (title=%s)"), *Title);
}

void UNetSubsystem::LeaveRoom()
{
	CS_RoomLeave Pkt{};
	Pkt.size = CS_ROOM_LEAVE_PACKET_SIZE;
	Pkt.id   = static_cast<uint16>(PACKET_ID::CS_ROOM_LEAVE_REQUEST);

	TArray<uint8> Buf;
	Buf.SetNumUninitialized(CS_ROOM_LEAVE_PACKET_SIZE);
	FMemory::Memcpy(Buf.GetData(), &Pkt, CS_ROOM_LEAVE_PACKET_SIZE);
	EnqueSendPacket(MoveTemp(Buf));

	// 로컬 상태 정리 후 로비로 복귀
	RoomMembers.Reset();
	CurrentRoomTitle.Empty();
	CurrentRoomHost.Empty();
	OnRoomMembersUpdated.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("[Net] 방 나가기 요청"));
	UGameplayStatics::OpenLevel(GetWorld(), FName("Lv1_Lobby"));
}

void UNetSubsystem::StartRoom()
{
	CS_RoomStart Pkt{};
	Pkt.size = CS_ROOM_START_PACKET_SIZE;
	Pkt.id   = static_cast<uint16>(PACKET_ID::CS_ROOM_START_REQUEST);

	TArray<uint8> Buf;
	Buf.SetNumUninitialized(CS_ROOM_START_PACKET_SIZE);
	FMemory::Memcpy(Buf.GetData(), &Pkt, CS_ROOM_START_PACKET_SIZE);
	EnqueSendPacket(MoveTemp(Buf));

	// 실제 레벨 이동은 서버의 SC_ROOM_START_NOTIFY 수신 시 처리
	UE_LOG(LogTemp, Log, TEXT("[Net] 방 시작 요청"));
}

void UNetSubsystem::Reconnect(const FString& IP)
{
	StoredServerIP = IP;

	if (Networker) Networker->Stop();
	if (Thread) { Thread->Kill(true); delete Thread; Thread = nullptr; }
	if (Networker) { delete Networker; Networker = nullptr; }

	Networker = new FNetworker(StoredServerIP);
	Thread = FRunnableThread::Create(Networker, TEXT("Network Thread"));

	UE_LOG(LogTemp, Log, TEXT("[Net] 서버 재연결: %s"), *StoredServerIP);
}

bool UNetSubsystem::IsConnected() const
{
	return Networker && Networker->IsConnected();
}

void UNetSubsystem::CopyStringToBuffer(char* Dest, int32 DestSize, const FString& Source)
{
	FTCHARToUTF8 Converter(*Source);
	int32 CopySize = FMath::Min(Converter.Length(), DestSize - 1);
	FMemory::Memcpy(Dest, Converter.Get(), CopySize);
	Dest[CopySize] = '\0'; // 안전하게 마지막 널 문자 삽입
}
