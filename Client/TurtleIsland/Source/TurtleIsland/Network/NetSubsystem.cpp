#include "Network/NetSubsystem.h"

#include "Network/ChatSubsystem.h"
#include "Network/ClientTransport.h"
#include "Network/NetSettings.h"

#include "Gameplay/AgentSkillComponent.h"
#include "Gameplay/HealthComponent.h"
#include "Kismet/GameplayStatics.h"

const FName UNetSubsystem::IngameLevelName(TEXT("Lv3_Ingame"));
const FName UNetSubsystem::LoginLevelName(TEXT("Lv0_Login"));

void UNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency(UChatSubsystem::StaticClass());
	ChatSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UChatSubsystem>() : nullptr;
	OpponentClass = UNetSettings::Get()->OpponentClass;
}

void UNetSubsystem::Deinitialize()
{
	ShutdownNetwork();
	Super::Deinitialize();
}

void UNetSubsystem::StartNetwork()
{
	if (Networker)
	{
		return;
	}

	Networker = new FClientTransport(StoredServerIP);
	Thread = FRunnableThread::Create(Networker, TEXT("Client Transport Thread"));
	UE_LOG(LogTemp, Log, TEXT("[Net] Transport thread started"));
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

	IsConnect = false;
	bLoginPending = false;
	PendingLoginElapsed = 0.f;
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
	if (ChatSubsystem)
	{
		ChatSubsystem->ResetHistory();
	}
	myId = 0;
}

void UNetSubsystem::StartSinglePlay()
{
	PlayMode = EPlayMode::Single;
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
	if (World)
	{
		UGameplayStatics::SetGamePaused(World, false);
	}

	ShutdownNetwork();
	ResetSessionState();
	PlayMode = EPlayMode::Multi;

	UE_LOG(LogTemp, Log, TEXT("[Mode] 타이틀로 복귀"));
	UGameplayStatics::OpenLevel(World, LoginLevelName);
}

void UNetSubsystem::Tick(float DeltaTime)
{
	PruneChatLines();
	PollPendingLogin(DeltaTime);
	ProcessIncomingPackets();
}

TStatId UNetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNetSubsystem, STATGROUP_Tickables);
}

void UNetSubsystem::EnqueSendPacket(TArray<uint8>&& packet)
{
	if (Networker && Networker->IsConnected())
	{
		Networker->EnqueueSendPacket(MoveTemp(packet));
	}
}

void UNetSubsystem::ProcessIncomingPackets()
{
	if (!Networker)
	{
		return;
	}

	IsConnect = Networker->IsConnected();
	while (true)
	{
		TArray<uint8> RecvData = Networker->DequeueRecvPacket();
		if (RecvData.Num() == 0)
		{
			break;
		}

		DispatchPacket(RecvData);
	}
}

void UNetSubsystem::DispatchPacket(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasAtLeastSize(RecvData, PACKET_HEADER_SIZE))
	{
		return;
	}

	const PacketHeader* Header = reinterpret_cast<const PacketHeader*>(RecvData.GetData());
	if (Header->size != RecvData.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Net] 잘못된 패킷 경계 거부 (header=%u actual=%d)"),
			Header->size, RecvData.Num());
		return;
	}

	switch (static_cast<PACKET_ID>(Header->id))
	{
	case PACKET_ID::SC_LOGIN_RESPONSE: HandleLoginResponse(RecvData); break;
	case PACKET_ID::SC_LOBBY_RESPONSE: HandleLobbyResponse(RecvData); break;
	case PACKET_ID::SC_ROOM_ENTER_RESPONSE: HandleRoomEnterResponse(RecvData); break;
	case PACKET_ID::SC_ROOM_ENTER_NOTIFY: HandleRoomEnterNotify(RecvData); break;
	case PACKET_ID::SC_ROOM_CHAT_RESPONSE: HandleRoomChatResponse(RecvData); break;
	case PACKET_ID::SC_ROOM_CHAT_NOTIFY: HandleRoomChatNotify(RecvData); break;
	case PACKET_ID::SC_MOVE_NOTIFY: HandleMoveNotify(RecvData); break;
	case PACKET_ID::SC_ATTACK_NOTIFY: HandleAttackNotify(RecvData); break;
	case PACKET_ID::SC_DAMAGE_NOTIFY: HandleDamageNotify(RecvData); break;
	case PACKET_ID::SC_ROOM_LEAVE_NOTIFY: HandleRoomLeaveNotify(RecvData); break;
	case PACKET_ID::SC_ROOM_START_NOTIFY: HandleRoomStartNotify(RecvData); break;
	case PACKET_ID::SC_NPC_CHAT: HandleNpcChat(RecvData); break;
	default:
		UE_LOG(LogTemp, Verbose, TEXT("[Net] 알 수 없는 패킷 id=%u"), Header->id);
		break;
	}
}

void UNetSubsystem::RequestLogin(const FString& UserID, const FString& Password)
{
	PendingLoginID = UserID.TrimStartAndEnd();
	PendingLoginPW = Password.TrimStartAndEnd();
	if (PendingLoginID.IsEmpty() || PendingLoginPW.IsEmpty())
	{
		OnLoginFailed.Broadcast(TEXT("ID 또는 PW를 입력해주세요."));
		return;
	}

	StoredUserID = PendingLoginID;
	PlayMode = EPlayMode::Multi;
	bLoginPending = true;
	PendingLoginElapsed = 0.f;

	if (IsConnected())
	{
		SendPendingLogin();
	}
	else
	{
		Reconnect(StoredServerIP);
	}
}

void UNetSubsystem::PollPendingLogin(float DeltaTime)
{
	if (!bLoginPending)
	{
		return;
	}

	if (IsConnected())
	{
		SendPendingLogin();
		return;
	}

	PendingLoginElapsed += DeltaTime;
	const bool bAttemptFailed = Networker && Networker->IsConnectionAttemptComplete() && !Networker->IsConnected();
	if (bAttemptFailed || PendingLoginElapsed >= LoginConnectTimeoutSec)
	{
		FailPendingLogin(TEXT("서버에 연결할 수 없습니다."));
	}
}

void UNetSubsystem::SendPendingLogin()
{
	if (!bLoginPending || !IsConnected())
	{
		return;
	}

	CS_Login LoginPacket{};
	LoginPacket.size = CS_LOGIN_PACKET_SIZE;
	LoginPacket.id = static_cast<uint16>(PACKET_ID::CS_LOGIN_REQUEST);
	FClientPacketCodec::CopyStringToBuffer(LoginPacket.userID, MAX_USER_ID_LEN + 1, PendingLoginID);
	FClientPacketCodec::CopyStringToBuffer(LoginPacket.userPW, MAX_USER_PW_LEN + 1, PendingLoginPW);

	bLoginPending = false;
	PendingLoginElapsed = 0.f;
	SendPacket(LoginPacket);
	UE_LOG(LogTemp, Log, TEXT("[Login] 패킷 전송"));
}

void UNetSubsystem::FailPendingLogin(const FString& Message)
{
	bLoginPending = false;
	PendingLoginElapsed = 0.f;
	ShutdownNetwork();
	OnLoginFailed.Broadcast(Message);
}

void UNetSubsystem::HandleLoginResponse(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_LOGIN_PACKET_SIZE))
	{
		return;
	}

	const SC_Login* Pkt = reinterpret_cast<const SC_Login*>(RecvData.GetData());
	if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
	{
		UE_LOG(LogTemp, Log, TEXT("[Net] 로그인 성공 (playerId=%u, new=%d)"), Pkt->playerId, Pkt->isNewAccount);
		myId = Pkt->playerId;
		OnLoginSucceeded.Broadcast(Pkt->isNewAccount != 0);
		return;
	}

	const FString Msg = (Pkt->result == static_cast<uint8>(ERROR_CODE::USER_ALREADY_LOGGED_IN))
		? TEXT("이미 접속 중인 계정입니다.")
		: TEXT("아이디 또는 비밀번호가 올바르지 않습니다.");
	OnLoginFailed.Broadcast(Msg);
}

void UNetSubsystem::HandleLobbyResponse(const TArray<uint8>& RecvData)
{
	TArray<FDecodedRoomInfo> DecodedRooms;
	if (!FClientPacketCodec::DecodeLobbyRooms(RecvData, DecodedRooms))
	{
		return;
	}

	RoomList.Empty();
	for (const FDecodedRoomInfo& Info : DecodedRooms)
	{
		FRoomData Data;
		Data.RoomId = Info.RoomId;
		Data.Title = Info.Title;
		Data.HostName = Info.HostName;
		Data.UserCount = Info.UserCount;
		RoomList.Add(MoveTemp(Data));
	}

	UE_LOG(LogTemp, Log, TEXT("[Net] 로비 응답 수신 - 방 개수: %d"), RoomList.Num());
	OnRoomListUpdated.Broadcast();
}

void UNetSubsystem::HandleRoomEnterResponse(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ROOM_ENTER_PACKET_SIZE))
	{
		return;
	}

	const SC_RoomEnter* Pkt = reinterpret_cast<const SC_RoomEnter*>(RecvData.GetData());
	if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
	{
		RoomMembers.Reset();
		FRoomMember Self;
		Self.UserID = StoredUserID;
		Self.UserIndex = static_cast<int32>(myId);
		Self.bIsLocal = true;
		Self.bIsHost = !CurrentRoomHost.IsEmpty() && CurrentRoomHost == StoredUserID;
		RoomMembers.Add(Self);
		OnRoomMembersUpdated.Broadcast();

		UGameplayStatics::OpenLevel(GetWorld(), FName("Lv2_Room"));
		return;
	}

	const FString Msg = (Pkt->result == static_cast<uint8>(ERROR_CODE::ROOM_TITLE_DUP))
		? TEXT("같은 제목의 방이 이미 있습니다.")
		: TEXT("방에 입장할 수 없습니다. (정원 초과 또는 게임 중)");
	OnRoomEnterFailed.Broadcast(Msg);
}

void UNetSubsystem::HandleRoomEnterNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ROOM_ENTER_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_RoomEnterNotify* Pkt = reinterpret_cast<const SC_RoomEnterNotify*>(RecvData.GetData());
	const int32 JoinedIdx = static_cast<int32>(Pkt->userIndex);
	const bool bExists = RoomMembers.ContainsByPredicate([JoinedIdx](const FRoomMember& M)
	{
		return M.UserIndex == JoinedIdx;
	});

	if (!bExists && JoinedIdx != static_cast<int32>(myId) && RoomMembers.Num() < MaxRoomMembers)
	{
		FRoomMember Member;
		Member.UserID = FClientPacketCodec::DecodeFixedUtf8(Pkt->userID, MAX_USER_ID_LEN + 1);
		Member.UserIndex = JoinedIdx;
		Member.bIsLocal = false;
		Member.bIsHost = !CurrentRoomHost.IsEmpty() && CurrentRoomHost == Member.UserID;
		RoomMembers.Add(Member);
		OnRoomMembersUpdated.Broadcast();
	}
}

void UNetSubsystem::HandleRoomChatResponse(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ROOM_CHAT_RESPONSE_PACKET_SIZE))
	{
		return;
	}

	const SC_RoomChatResponse* Pkt = reinterpret_cast<const SC_RoomChatResponse*>(RecvData.GetData());
	if (Pkt->result != static_cast<uint8>(ERROR_CODE::NONE))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Net] 채팅 전송 실패 (result=%d)"), Pkt->result);
	}
}

void UNetSubsystem::HandleRoomChatNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ROOM_CHAT_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_RoomChatNotify* Pkt = reinterpret_cast<const SC_RoomChatNotify*>(RecvData.GetData());
	const FString Msg = FClientPacketCodec::DecodeFixedUtf8(Pkt->chatMsg, MAX_CHAT_MSG_LEN + 1);
	if (!Msg.IsEmpty())
	{
		EmitChat(FString::Printf(TEXT("[%s] %s"), *FindUserNameByIndex(static_cast<int32>(Pkt->userIndex)), *Msg));
	}
}

void UNetSubsystem::HandleMoveNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_MOVE_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_MoveNotify* Pkt = reinterpret_cast<const SC_MoveNotify*>(RecvData.GetData());
	const FRotator NewRotation(0.f, Pkt->yaw, 0.f);

	if (AServerAgent** Found = players.Find(Pkt->userIndex))
	{
		if (*Found)
		{
			(*Found)->UpdateNetState(Pkt->x, Pkt->y, Pkt->yaw);
		}
		return;
	}

	if (OpponentClass && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AServerAgent* NewAgent = GetWorld()->SpawnActor<AServerAgent>(
			OpponentClass, FVector(Pkt->x, Pkt->y, 0.f), NewRotation, Params);
		if (NewAgent)
		{
			NewAgent->UpdateNetState(Pkt->x, Pkt->y, Pkt->yaw);
			players.Emplace(Pkt->userIndex, NewAgent);
		}
	}
}

void UNetSubsystem::HandleAttackNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ATTACK_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_AttackNotify* Pkt = reinterpret_cast<const SC_AttackNotify*>(RecvData.GetData());
	if (ATurtleAgent* Caster = FindAgentById(Pkt->casterId))
	{
		if (UAgentSkillComponent* Skill = Caster->FindComponentByClass<UAgentSkillComponent>())
		{
			Skill->PlaySkill(static_cast<int32>(Pkt->skillId), Pkt->yaw);
		}
	}
}

void UNetSubsystem::HandleDamageNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_DAMAGE_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_DamageNotify* Pkt = reinterpret_cast<const SC_DamageNotify*>(RecvData.GetData());
	ATurtleAgent* Target = FindAgentById(Pkt->targetId);
	if (!Target)
	{
		return;
	}

	if (UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>())
	{
		AActor* Causer = FindAgentById(Pkt->causerId);
		Health->ApplyServerDamage(Pkt->remainHP, Pkt->damage, Causer);
	}
}

void UNetSubsystem::HandleRoomLeaveNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ROOM_LEAVE_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_RoomLeaveNotify* Pkt = reinterpret_cast<const SC_RoomLeaveNotify*>(RecvData.GetData());
	if (AServerAgent** Found = players.Find(Pkt->userIndex))
	{
		if (*Found)
		{
			(*Found)->Destroy();
		}
		players.Remove(Pkt->userIndex);
	}

	const int32 LeftIdx = static_cast<int32>(Pkt->userIndex);
	const int32 Removed = RoomMembers.RemoveAll([LeftIdx](const FRoomMember& M)
	{
		return M.UserIndex == LeftIdx;
	});
	if (Removed > 0)
	{
		OnRoomMembersUpdated.Broadcast();
	}
}

void UNetSubsystem::HandleRoomStartNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ROOM_START_PACKET_SIZE))
	{
		return;
	}

	const SC_RoomStart* Pkt = reinterpret_cast<const SC_RoomStart*>(RecvData.GetData());
	if (Pkt->result == static_cast<uint8>(ERROR_CODE::NONE))
	{
		UGameplayStatics::OpenLevel(GetWorld(), IngameLevelName);
	}
}

void UNetSubsystem::HandleNpcChat(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasAtLeastSize(RecvData, SC_NPC_CHAT_PACKET_SIZE + 1))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Npc] 빈 응답 수신"));
		return;
	}

	const FString Say = FClientPacketCodec::DecodeUtf8Payload(RecvData, SC_NPC_CHAT_PACKET_SIZE);
	OnNpcChatReceived.Broadcast(Say);
}

void UNetSubsystem::NotifyIngameReady(float X, float Y, float Yaw)
{
	CS_IngameReady Pkt{};
	Pkt.size = CS_INGAME_READY_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_INGAME_READY);
	Pkt.x = X;
	Pkt.y = Y;
	Pkt.yaw = Yaw;

	SendPacket(Pkt);
	UE_LOG(LogTemp, Log, TEXT("[Net] 인게임 준비 완료 통지"));
}

void UNetSubsystem::EnterLobby()
{
	RefreshRooms();
	UGameplayStatics::OpenLevel(GetWorld(), FName("Lv1_Lobby"));
}

void UNetSubsystem::RefreshRooms()
{
	CS_LobbyEnter LobbyPkt{};
	LobbyPkt.size = CS_LOBBY_ENTER_PACKET_SIZE;
	LobbyPkt.id = static_cast<uint16>(PACKET_ID::CS_LOBBY_ENTER_REQUEST);

	SendPacket(LobbyPkt);
}

void UNetSubsystem::EnterRoom(int32 RoomIndex)
{
	CurrentRoomTitle.Empty();
	CurrentRoomHost.Empty();
	for (const FRoomData& Room : RoomList)
	{
		if (Room.RoomId == RoomIndex)
		{
			CurrentRoomTitle = Room.Title;
			CurrentRoomHost = Room.HostName;
			break;
		}
	}

	CS_RoomEnter Pkt{};
	Pkt.size = CS_ROOM_ENTER_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_ROOM_ENTER_REQUEST);
	Pkt.roomIndex = static_cast<uint32>(RoomIndex);

	SendPacket(Pkt);
	UE_LOG(LogTemp, Log, TEXT("[Net] 방 입장 요청 (roomIndex=%d)"), RoomIndex);
}

void UNetSubsystem::CreateRoom(const FString& Title)
{
	CurrentRoomTitle = Title;
	CurrentRoomHost = StoredUserID;

	CS_RoomCreate Pkt{};
	Pkt.size = CS_ROOM_CREATE_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_ROOM_CREATE_REQUEST);
	FClientPacketCodec::CopyStringToBuffer(Pkt.roomTitle, MAX_ROOM_TITLE_LEN, Title);

	SendPacket(Pkt);
	UE_LOG(LogTemp, Log, TEXT("[Net] 방 생성 요청 (title=%s)"), *Title);
}

void UNetSubsystem::LeaveRoom()
{
	CS_RoomLeave Pkt{};
	Pkt.size = CS_ROOM_LEAVE_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_ROOM_LEAVE_REQUEST);

	SendPacket(Pkt);

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
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_ROOM_START_REQUEST);

	SendPacket(Pkt);
	UE_LOG(LogTemp, Log, TEXT("[Net] 방 시작 요청"));
}

void UNetSubsystem::Reconnect(const FString& IP)
{
	StoredServerIP = IP;
	PlayMode = EPlayMode::Multi;

	ShutdownNetwork();
	StartNetwork();
	UE_LOG(LogTemp, Log, TEXT("[Net] 서버 재연결: %s"), *StoredServerIP);
}

bool UNetSubsystem::IsConnected() const
{
	return Networker && Networker->IsConnected();
}

ATurtleAgent* UNetSubsystem::FindAgentById(uint32 Id) const
{
	if (Id == myId)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* PC = World->GetFirstPlayerController())
			{
				return Cast<ATurtleAgent>(PC->GetPawn());
			}
		}
		return nullptr;
	}

	AServerAgent* const* Found = players.Find(Id);
	return Found ? *Found : nullptr;
}

void UNetSubsystem::SendAttack(int32 SkillId, int64 TargetId, float Yaw)
{
	if (IsSinglePlay() || !IsConnected())
	{
		return;
	}

	CS_Attack Pkt{};
	Pkt.size = CS_ATTACK_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_ATTACK);
	Pkt.skillId = static_cast<uint16>(SkillId);
	Pkt.targetId = static_cast<uint32>(TargetId);
	Pkt.yaw = Yaw;

	SendPacket(Pkt);
}

void UNetSubsystem::SendChat(const FString& Message)
{
	const FString Trimmed = Message.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return;
	}

	const FString MyName = StoredUserID.IsEmpty() ? TEXT("나") : StoredUserID;
	if (IsSinglePlay() || !IsConnected())
	{
		EmitChat(FString::Printf(TEXT("[%s] %s"), *MyName, *Trimmed));
		return;
	}

	CS_RoomChat Pkt{};
	Pkt.size = CS_ROOM_CHAT_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_ROOM_CHAT_REQUEST);
	FClientPacketCodec::CopyStringToBuffer(Pkt.chatMsg, MAX_CHAT_MSG_LEN + 1, Trimmed);

	SendPacket(Pkt);
}

void UNetSubsystem::SendNpcChat(int32 NpcIndex, const FString& Message)
{
	TrySendNpcChat(NpcIndex, Message);
}

bool UNetSubsystem::TrySendNpcChat(int32 NpcIndex, const FString& Message)
{
	const FString Trimmed = Message.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return false;
	}

	if (IsSinglePlay() || !IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Npc] 미접속 상태 - NPC 대화를 보낼 수 없다"));
		return false;
	}

	const int32 MaxText = MaxPacketBytes - CS_NPC_CHAT_PACKET_SIZE;
	TArray<uint8> Buf;
	Buf.SetNumZeroed(CS_NPC_CHAT_PACKET_SIZE + MaxText);

	auto* Pkt = reinterpret_cast<CS_NpcChat*>(Buf.GetData());
	Pkt->id = static_cast<uint16>(PACKET_ID::CS_NPC_CHAT);
	Pkt->userIndex = myId;
	Pkt->npcIndex = static_cast<uint32>(NpcIndex);

	const int32 TextLen = FClientPacketCodec::AppendUtf8PayloadClamped(Buf, CS_NPC_CHAT_PACKET_SIZE, Trimmed);
	Buf.SetNum(CS_NPC_CHAT_PACKET_SIZE + TextLen, EAllowShrinking::No);
	Pkt = reinterpret_cast<CS_NpcChat*>(Buf.GetData());
	Pkt->size = static_cast<uint16>(Buf.Num());

	EnqueSendPacket(MoveTemp(Buf));
	UE_LOG(LogTemp, Log, TEXT("[Npc] 대화 전송 (npcIndex=%d, %d바이트): %s"), NpcIndex, TextLen, *Trimmed);
	return true;
}

void UNetSubsystem::EmitChat(const FString& Line)
{
	if (!ChatSubsystem && GetGameInstance())
	{
		ChatSubsystem = GetGameInstance()->GetSubsystem<UChatSubsystem>();
	}

	if (ChatSubsystem)
	{
		ChatSubsystem->AddMessage(Line, MaxChatLines);
		SyncLegacyChatLines();
	}
	else
	{
		ChatLines.Add(Line);
		ChatTimes.Add(FPlatformTime::Seconds());
		const int32 Cap = FMath::Max(1, MaxChatLines);
		while (ChatLines.Num() > Cap)
		{
			ChatLines.RemoveAt(0);
			ChatTimes.RemoveAt(0);
		}
	}

	OnChatMessage.Broadcast(Line);
}

void UNetSubsystem::PruneChatLines()
{
	if (ChatSubsystem)
	{
		if (ChatSubsystem->PruneExpired(ChatLifeSec))
		{
			SyncLegacyChatLines();
			OnChatMessage.Broadcast(FString());
		}
		return;
	}

	if (ChatLifeSec <= 0.f || ChatLines.Num() == 0)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	bool bRemoved = false;
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

void UNetSubsystem::SyncLegacyChatLines()
{
	ChatLines = ChatSubsystem ? ChatSubsystem->GetLines() : TArray<FString>();
	ChatTimes.Reset();
	if (ChatSubsystem)
	{
		for (const FChatEntry& Entry : ChatSubsystem->GetMessages())
		{
			ChatTimes.Add(Entry.AddedAt);
		}
	}
}

FString UNetSubsystem::GetChatText() const
{
	return ChatSubsystem ? ChatSubsystem->GetChatText() : FString::Join(ChatLines, TEXT("\n"));
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
	FClientPacketCodec::CopyStringToBuffer(Dest, DestSize, Source);
}
