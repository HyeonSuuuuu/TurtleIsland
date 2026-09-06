// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/NetSubsystem.h"
#include "Network/NetSettings.h"

#include <Protocol.h>

#include "Network/FNetworker.h"
#include "Kismet/GameplayStatics.h"

const FName UNetSubsystem::IngameLevelName(TEXT("Lv3_Ingame"));
const FName UNetSubsystem::LoginLevelName(TEXT("Lv0_Login"));

void UNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	OpponentClass = UNetSettings::Get()->OpponentClass;

	// 소켓은 여기서 열지 않는다. 싱글 모드로 시작할 수 있으므로
	// 실제 접속은 멀티 로그인 시점(Reconnect)까지 미룬다.
}

void UNetSubsystem::Deinitialize()
{
	ShutdownNetwork();
	Super::Deinitialize();
}

void UNetSubsystem::StartNetwork()
{
	if (Networker) return;

	Networker = new FNetworker(StoredServerIP);
	Thread = FRunnableThread::Create(Networker, TEXT("Network Thread"));

	UE_LOG(LogTemp, Warning, TEXT("Network Thread Started!"));
}

void UNetSubsystem::ShutdownNetwork()
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
}

void UNetSubsystem::ResetSessionState()
{
	RoomList.Reset();
	RoomMembers.Reset();
	CurrentRoomTitle.Empty();
	CurrentRoomHost.Empty();
	players.Reset();
	ChatLines.Reset();
	ChatTimes.Reset();
	myId = 0;
}

void UNetSubsystem::StartSinglePlay()
{
	PlayMode = EPlayMode::Single;

	// 멀티에서 남은 연결/상태를 모두 정리
	ShutdownNetwork();
	ResetSessionState();

	UE_LOG(LogTemp, Log, TEXT("[Mode] 싱글 플레이 시작"));
	UGameplayStatics::OpenLevel(GetWorld(), IngameLevelName);
}

void UNetSubsystem::StartMultiPlay()
{
	PlayMode = EPlayMode::Multi;
}

void UNetSubsystem::ReturnToTitle()
{
	UWorld* World = GetWorld();

	// 일시정지 상태로 나가면 로그인 화면까지 멈춘 채로 넘어간다
	if (World)
		UGameplayStatics::SetGamePaused(World, false);

	ShutdownNetwork();
	ResetSessionState();
	PlayMode = EPlayMode::Multi;

	UE_LOG(LogTemp, Log, TEXT("[Mode] 타이틀로 복귀"));
	UGameplayStatics::OpenLevel(World, LoginLevelName);
}

void UNetSubsystem::Tick(float DeltaTime)
{
	PruneChatLines();

	if (Networker == nullptr) return;

	while (true)
	{
		TArray<uint8> RecvData = Networker->DequeRecvPacket();
		if (RecvData.Num() == 0) break;

		// 큐엔 헤더 이상 크기만 들어오지만, 어긋나면 버리고 다음 패킷으로
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
		case PACKET_ID::SC_ROOM_CHAT_RESPONSE:
			{
				SC_RoomChatResponse* Pkt = reinterpret_cast<SC_RoomChatResponse*>(RecvData.GetData());
				if (Pkt->result != static_cast<uint8>(ERROR_CODE::NONE))
				{
					UE_LOG(LogTemp, Warning, TEXT("[Net] 채팅 전송 실패 (result=%d)"), Pkt->result);
				}
			break;
			}
		case PACKET_ID::SC_ROOM_CHAT_NOTIFY:
			{
				SC_RoomChatNotify* Pkt = reinterpret_cast<SC_RoomChatNotify*>(RecvData.GetData());

				const int32   SenderIdx = static_cast<int32>(Pkt->userIndex);
				const FString Msg       = FString(UTF8_TO_TCHAR(Pkt->chatMsg));
				if (!Msg.IsEmpty())
				{
					EmitChat(FString::Printf(TEXT("[%s] %s"), *FindUserNameByIndex(SenderIdx), *Msg));
				}
			break;
			}
		case PACKET_ID::SC_MOVE_NOTIFY:
			{
				SC_MoveNotify* Pkt = reinterpret_cast<SC_MoveNotify*>(RecvData.GetData());

				// 서버는 보낸 사람에게 SC_MOVE_NOTIFY를 보내지 않으므로
				// 수신되는 건 항상 다른 플레이어의 움직임
				// 서버는 월드 2D (x, y) 좌표 사용 → Unreal (X, Y) 그대로, 높이(Z)는 바닥 고정
				// Z 는 여기서 정하지 않는다. UNetInputProvider 가 GroundDetector 로 지형에 맞춰 구한다.
				// (예전에는 OpponentGroundZ = 92.f 상수라 언덕에서 파묻히거나 떴다)
				const FRotator NewRotation(0.f, Pkt->yaw, 0.f);

				if (players.Contains(Pkt->userIndex))
				{
					if (AServerAgent* Agent = players[Pkt->userIndex])
						Agent->UpdateNetState(Pkt->x, Pkt->y, Pkt->yaw);
				}
				else
				{
					// 처음 보는 플레이어 → 스폰
					if (OpponentClass && GetWorld())
					{
						// 첫 프레임에 지면을 찾을 때까지 쓸 임시 높이
						const FVector SpawnLocation(Pkt->x, Pkt->y, OpponentGroundZ);

						FActorSpawnParameters Params;
						Params.SpawnCollisionHandlingOverride =
							ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
						AServerAgent* NewAgent = GetWorld()->SpawnActor<AServerAgent>(
							OpponentClass, SpawnLocation, NewRotation, Params);
						if (NewAgent)
						{
							NewAgent->UpdateNetState(Pkt->x, Pkt->y, Pkt->yaw);
							players.Emplace(Pkt->userIndex, NewAgent);
							UE_LOG(LogTemp, Log, TEXT("[Net] 상대방 스폰 (userId=%d)"), Pkt->userIndex);
						}
					}
				}
			break;
			}
		case PACKET_ID::SC_ROOM_LEAVE_NOTIFY:
			{
				SC_RoomLeaveNotify* Pkt = reinterpret_cast<SC_RoomLeaveNotify*>(RecvData.GetData());

				AServerAgent** Found = players.Find(Pkt->userIndex);
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
					UGameplayStatics::OpenLevel(GetWorld(), IngameLevelName);

				}
				UE_LOG(LogTemp, Log, TEXT("Ingame 패킷 수신"));
			}
			break;
		case PACKET_ID::SC_NPC_CHAT:
			{
				// 헤더 뒤가 전부 UTF-8 텍스트다. 널 종단이 없으므로 남은 길이로 자른다.
				const int32 TextLen = RecvData.Num() - SC_NPC_CHAT_PACKET_SIZE;
				if (TextLen <= 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("[Npc] 빈 응답 수신"));
					break;
				}

				// 널을 직접 붙여 변환한다. 길이 지정 변환은 널 종단을 보장하지 않는다.
				TArray<uint8> Utf8Bytes;
				Utf8Bytes.Append(RecvData.GetData() + SC_NPC_CHAT_PACKET_SIZE, TextLen);
				Utf8Bytes.Add(0);
				const FString Say = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Utf8Bytes.GetData()));

				UE_LOG(LogTemp, Log, TEXT("[Npc] 답변 수신 (%d바이트): %s"), TextLen, *Say);
				OnNpcChatReceived.Broadcast(Say);
			}
			break;
		}
	}

}

TStatId UNetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNetSubsystem, STATGROUP_Tickables);
}

void UNetSubsystem::EnqueSendPacket(TArray<uint8>&& packet)
{
	// 싱글 모드거나 아직 접속 전이면 소켓 스레드가 없다
	if (Networker == nullptr) return;

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

	SendPacket(Pkt);

	UE_LOG(LogTemp, Log, TEXT("[Net] 인게임 준비 완료 통지"));
}

void UNetSubsystem::EnterLobby()
{
	// CS_LOBBY_ENTER 전송 후 로비 레벨 로드
	CS_LobbyEnter LobbyPkt{};
	LobbyPkt.size = CS_LOBBY_ENTER_PACKET_SIZE;
	LobbyPkt.id   = static_cast<uint16>(PACKET_ID::CS_LOBBY_ENTER_REQUEST);

	SendPacket(LobbyPkt);

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

	SendPacket(Pkt);

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

	SendPacket(Pkt);

	UE_LOG(LogTemp, Log, TEXT("[Net] 방 생성 요청 (title=%s)"), *Title);
}

void UNetSubsystem::LeaveRoom()
{
	CS_RoomLeave Pkt{};
	Pkt.size = CS_ROOM_LEAVE_PACKET_SIZE;
	Pkt.id   = static_cast<uint16>(PACKET_ID::CS_ROOM_LEAVE_REQUEST);

	SendPacket(Pkt);

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

	SendPacket(Pkt);

	// 실제 레벨 이동은 서버의 SC_ROOM_START_NOTIFY 수신 시 처리
	UE_LOG(LogTemp, Log, TEXT("[Net] 방 시작 요청"));
}

void UNetSubsystem::Reconnect(const FString& IP)
{
	StoredServerIP = IP;

	// 접속을 시도한다는 건 멀티 모드라는 뜻
	PlayMode = EPlayMode::Multi;

	ShutdownNetwork();
	StartNetwork();

	UE_LOG(LogTemp, Log, TEXT("[Net] 서버 재연결: %s"), *StoredServerIP);
}

bool UNetSubsystem::IsConnected() const
{
	return Networker && Networker->IsConnected();
}

void UNetSubsystem::SendChat(const FString& Message)
{
	const FString Trimmed = Message.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) return;

	const FString MyName = StoredUserID.IsEmpty() ? TEXT("나") : StoredUserID;

	// 싱글은 서버가 없으니 로컬에만 찍는다
	if (IsSinglePlay() || !IsConnected())
	{
		EmitChat(FString::Printf(TEXT("[%s] %s"), *MyName, *Trimmed));
		return;
	}

	// 서버가 보낸 사람에게도 SC_ROOM_CHAT_NOTIFY 를 되돌려주므로 로컬 에코는 하지 않는다
	CS_RoomChat Pkt{};
	Pkt.size = CS_ROOM_CHAT_PACKET_SIZE;
	Pkt.id   = static_cast<uint16>(PACKET_ID::CS_ROOM_CHAT_REQUEST);
	CopyStringToBuffer(Pkt.chatMsg, MAX_CHAT_MSG_LEN + 1, Trimmed);

	SendPacket(Pkt);
}

void UNetSubsystem::SendNpcChat(int32 NpcIndex, const FString& Message)
{
	const FString Trimmed = Message.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) return;

	if (IsSinglePlay() || !IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Npc] 미접속 상태 — NPC 대화를 보낼 수 없다"));
		return;
	}

	// 헤더 뒤에 UTF-8 텍스트를 그대로 붙인다. 서버 수신 버퍼가 1024B 라
	// 넘치면 통째로 버려지므로 여기서 잘라 보낸다.
	FTCHARToUTF8 Utf8(*Trimmed);
	int32 TextLen = Utf8.Length();
	const int32 MaxText = MaxPacketBytes - CS_NPC_CHAT_PACKET_SIZE;
	if (TextLen > MaxText)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Npc] 메시지가 길어 %d→%d바이트로 자른다"), TextLen, MaxText);
		TextLen = MaxText;
	}

	const int32 Total = CS_NPC_CHAT_PACKET_SIZE + TextLen;
	TArray<uint8> Buf;
	Buf.SetNumZeroed(Total);

	auto* Pkt      = reinterpret_cast<CS_NpcChat*>(Buf.GetData());
	Pkt->size      = static_cast<uint16>(Total);
	Pkt->id        = static_cast<uint16>(PACKET_ID::CS_NPC_CHAT);
	Pkt->userIndex = myId;
	Pkt->npcIndex  = static_cast<uint32>(NpcIndex);
	if (TextLen > 0)
		FMemory::Memcpy(Buf.GetData() + CS_NPC_CHAT_PACKET_SIZE, Utf8.Get(), TextLen);

	EnqueSendPacket(MoveTemp(Buf));
	UE_LOG(LogTemp, Log, TEXT("[Npc] 대화 전송 (npcIndex=%d, %d바이트): %s"),
		NpcIndex, TextLen, *Trimmed);
}

void UNetSubsystem::EmitChat(const FString& Line)
{
	ChatLines.Add(Line);
	ChatTimes.Add(FPlatformTime::Seconds());

	const int32 Cap = FMath::Max(1, MaxChatLines);
	while (ChatLines.Num() > Cap)
	{
		ChatLines.RemoveAt(0);
		ChatTimes.RemoveAt(0);
	}

	OnChatMessage.Broadcast(Line);
}

void UNetSubsystem::PruneChatLines()
{
	if (ChatLifeSec <= 0.f || ChatLines.Num() == 0) return;

	const double Now = FPlatformTime::Seconds();
	bool bRemoved = false;

	// 오래된 것이 앞에 있으므로 앞에서부터 걷어낸다
	while (ChatTimes.Num() > 0 && Now - ChatTimes[0] > ChatLifeSec)
	{
		ChatLines.RemoveAt(0);
		ChatTimes.RemoveAt(0);
		bRemoved = true;
	}

	if (bRemoved)
	{
		OnChatMessage.Broadcast(FString());
	}
}

FString UNetSubsystem::GetChatText() const
{
	return FString::Join(ChatLines, TEXT("\n"));
}

FString UNetSubsystem::FindUserNameByIndex(int32 UserIndex) const
{
	for (const FRoomMember& M : RoomMembers)
	{
		if (M.UserIndex == UserIndex)
		{
			return M.UserID;
		}
	}
	return FString::Printf(TEXT("유저 %d"), UserIndex);
}

void UNetSubsystem::CopyStringToBuffer(char* Dest, int32 DestSize, const FString& Source)
{
	FTCHARToUTF8 Converter(*Source);
	int32 CopySize = FMath::Min(Converter.Length(), DestSize - 1);
	FMemory::Memcpy(Dest, Converter.Get(), CopySize);
	Dest[CopySize] = '\0'; // 안전하게 마지막 널 문자 삽입
}
