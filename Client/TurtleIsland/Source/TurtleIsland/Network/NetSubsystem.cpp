#include "Network/NetSubsystem.h"
#include <cstring>

#include "Network/ChatSubsystem.h"
#include "Network/ClientTransport.h"
#include "Network/NetSettings.h"
#include "Network/NetSyncComponent.h"
#include "Network/NpcSubsystem.h"
#include "Character/ServerMonster.h"

#include "Gameplay/AgentSkillComponent.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/PlayerStatsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayRules.h"
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
	ResetNpcWorld();
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
	ResetNpcWorld();
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
	if (!IsConnect)
	{
		ResetNpcWorld();
		return;
	}
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

	const auto Id = static_cast<PACKET_ID>(Header->id);
	if (Id == PACKET_ID::SC_MOVE_NOTIFY || Id == PACKET_ID::SC_RESPAWN_NOTIFY || Id == PACKET_ID::SC_ATTACK_NOTIFY ||
		Id == PACKET_ID::SC_DAMAGE_NOTIFY || Id == PACKET_ID::SC_PLAYER_STATE || Id == PACKET_ID::SC_ACTION_RESULT ||
		Id == PACKET_ID::SC_HEALTH_STATE || Id == PACKET_ID::SC_PLAYER_INFO)
	{
		const UNpcSubsystem* Npcs = GetWorld() ? GetWorld()->GetSubsystem<UNpcSubsystem>() : nullptr;
		if (!Npcs || !Npcs->IsReady()) return;
	}
	switch (Id)
	{
	case PACKET_ID::SC_LOGIN_RESPONSE: HandleLoginResponse(RecvData); break;
	case PACKET_ID::SC_LOBBY_RESPONSE: HandleLobbyResponse(RecvData); break;
	case PACKET_ID::SC_ROOM_ENTER_RESPONSE: HandleRoomEnterResponse(RecvData); break;
	case PACKET_ID::SC_ROOM_ENTER_NOTIFY: HandleRoomEnterNotify(RecvData); break;
	case PACKET_ID::SC_ROOM_CHAT_RESPONSE: HandleRoomChatResponse(RecvData); break;
	case PACKET_ID::SC_ROOM_CHAT_NOTIFY: HandleRoomChatNotify(RecvData); break;
	case PACKET_ID::SC_MOVE_NOTIFY: HandleMoveNotify(RecvData); break;
	case PACKET_ID::SC_RESPAWN_NOTIFY: HandleMoveNotify(RecvData, true); break;
	case PACKET_ID::SC_ATTACK_NOTIFY: HandleAttackNotify(RecvData); break;
	case PACKET_ID::SC_DAMAGE_NOTIFY: HandleDamageNotify(RecvData); break;
	case PACKET_ID::SC_PLAYER_STATE: HandlePlayerState(RecvData); break;
	case PACKET_ID::SC_ACTION_RESULT: HandleActionResult(RecvData); break;
	case PACKET_ID::SC_HEALTH_STATE: HandleHealthState(RecvData); break;
	case PACKET_ID::SC_PLAYER_INFO: HandlePlayerInfo(RecvData); break;
	case PACKET_ID::SC_ROOM_LEAVE_NOTIFY: HandleRoomLeaveNotify(RecvData); break;
	case PACKET_ID::SC_ROOM_START_NOTIFY: HandleRoomStartNotify(RecvData); break;
	case PACKET_ID::SC_NPC_CHAT: HandleNpcChat(RecvData); break;
	case PACKET_ID::SC_SHOP_PRICES: HandleShopPrices(RecvData); break;
	case PACKET_ID::SC_NPC_SPAWN:
	case PACKET_ID::SC_NPC_MOVE:
	case PACKET_ID::SC_NPC_DESPAWN:
		if (UWorld* World = GetWorld())
		{
			if (UNpcSubsystem* Npcs = World->GetSubsystem<UNpcSubsystem>())
			{
				Npcs->HandlePacket(RecvData);
			}
		}
		break;
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
	if (!IsConnected())
	{
		Reconnect(StoredServerIP);
	}
	// Reconnect calls ShutdownNetwork, which clears any previous pending login.
	bLoginPending = true;
	PendingLoginElapsed = 0.f;
	SendPendingLogin();
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

void UNetSubsystem::HandleMoveNotify(const TArray<uint8>& RecvData, bool bRespawn)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_MOVE_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_MoveNotify* Pkt = reinterpret_cast<const SC_MoveNotify*>(RecvData.GetData());
	if (!GameplayRules::ValidPose(Pkt->x, Pkt->y, Pkt->z, Pkt->yaw, Pkt->velocityZ) || Pkt->airborne > 1 ||
		Pkt->maxHP <= 0 || Pkt->hp < 0 || Pkt->hp > Pkt->maxHP) return;
	if (bRespawn && (Pkt->lifeId == 0 || Pkt->hp == 0 || Pkt->airborne || Pkt->velocityZ != 0.f)) return;
	if (Pkt->userIndex == myId && Pkt->lifeId < PlayerLifeId) return;
	const uint32* PreviousLife = PlayerLives.Find(Pkt->userIndex);
	if (PreviousLife && (Pkt->lifeId < *PreviousLife || (bRespawn && Pkt->lifeId == *PreviousLife))) return;
	const bool bNewLife = bRespawn || (PreviousLife && Pkt->lifeId > *PreviousLife);
	PlayerLives.Add(Pkt->userIndex, Pkt->lifeId);
	const FVector Position(Pkt->x, Pkt->y, Pkt->z);
	const FRotator NewRotation(0.f, Pkt->yaw, 0.f);
	if (Pkt->userIndex == myId)
	{
		PlayerLifeId = Pkt->lifeId;
		// Own movement packets are authoritative spawn/rejection corrections, not echoes.
		if (ATurtleAgent* Self = FindAgentById(myId))
		{
			if (bNewLife)
			{
				Self->ResetForRespawn(Position, Pkt->hp);
				if (UNetSyncComponent* Sync = Self->FindComponentByClass<UNetSyncComponent>()) Sync->ResetForRespawn();
			}
			Self->SetActorLocationAndRotation(Position, NewRotation, false, nullptr, ETeleportType::TeleportPhysics);
			ApplyHealthState(Self, Pkt->hp, Pkt->maxHP);
			if (UCharacterMovementComponent* Movement = Self->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
				Movement->SetMovementMode(Pkt->hp == 0 ? MOVE_None : Pkt->airborne ? MOVE_Falling : MOVE_Walking);
				Movement->Velocity.Z = Pkt->hp == 0 ? 0.f : Pkt->velocityZ;
			}
		}
		return;
	}

	if (AServerAgent** Found = players.Find(Pkt->userIndex))
	{
		if (IsValid(*Found))
		{
			if (bNewLife)
			{
				(*Found)->ResetForRespawn(Position, Pkt->hp);
				(*Found)->SetActorLocationAndRotation(Position, NewRotation, false, nullptr, ETeleportType::TeleportPhysics);
			}
			(*Found)->UpdateNetPlayerState(Position, Pkt->yaw, Pkt->velocityZ, Pkt->airborne != 0);
			ApplyHealthState(*Found, Pkt->hp, Pkt->maxHP);
		}
		return;
	}

	if (OpponentClass && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AServerAgent* NewAgent = GetWorld()->SpawnActor<AServerAgent>(
			OpponentClass, FVector(Pkt->x, Pkt->y, Pkt->z), NewRotation, Params);
		if (NewAgent)
		{
			NewAgent->UpdateNetPlayerState(FVector(Pkt->x, Pkt->y, Pkt->z), Pkt->yaw, Pkt->velocityZ, Pkt->airborne != 0);
			players.Emplace(Pkt->userIndex, NewAgent);
			ApplyHealthState(NewAgent, Pkt->hp, Pkt->maxHP);
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
	if (!FMath::IsFinite(Pkt->yaw)) return;
	if (ATurtleAgent* Caster = FindEntity(Pkt->casterKind, Pkt->casterId))
	{
		if (AServerMonster* Monster = Cast<AServerMonster>(Caster))
		{
			Monster->PlayAttack(Pkt->yaw);
			return;
		}
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
	if (Pkt->damage < 0 || Pkt->maxHP <= 0 || Pkt->remainHP < 0 || Pkt->remainHP > Pkt->maxHP) return;
	ATurtleAgent* Target = FindEntity(Pkt->targetKind, Pkt->targetId);
	if (!Target)
	{
		return;
	}

	if (UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>())
	{
		AActor* Causer = FindEntity(Pkt->causerKind, Pkt->causerId);
		Health->MaxHP = Pkt->maxHP;
		OnDamageNumber.Broadcast(Target->GetActorLocation(), Pkt->damage, Pkt->flags);
		Health->ApplyServerDamage(Pkt->remainHP, Pkt->damage, Causer);
	}
}

ATurtleAgent* UNetSubsystem::FindEntity(uint8 Kind, uint32 Id) const
{
	if (Kind == ENTITY_PLAYER) return FindAgentById(Id);
	if (Kind == ENTITY_NPC && GetWorld())
		if (const UNpcSubsystem* Npcs = GetWorld()->GetSubsystem<UNpcSubsystem>()) return Npcs->FindNpc(Id);
	return nullptr;
}

void UNetSubsystem::ApplyHealthState(ATurtleAgent* Agent, int32 HP, int32 MaxHP)
{
	if (!Agent) return;
	if (UHealthComponent* Health = Agent->FindComponentByClass<UHealthComponent>())
	{
		Health->MaxHP = MaxHP;
		if (HP == 0 && !Health->IsDead()) Health->ApplyServerDeath(nullptr);
		else if (HP > 0 && Health->IsDead()) Agent->ResetForRespawn(Agent->GetActorLocation(), HP);
		else Health->ResetTo(MaxHP, HP);
	}
}

void UNetSubsystem::HandleHealthState(const TArray<uint8>& Data)
{
	if (Data.Num() != SC_HEALTH_STATE_PACKET_SIZE) return;
	const auto& State = *reinterpret_cast<const SC_HealthState*>(Data.GetData());
	if (State.playerId == myId || State.maxHP <= 0 || State.hp < 0 || State.hp > State.maxHP) return;
	ApplyHealthState(FindAgentById(State.playerId), State.hp, State.maxHP);
}

void UNetSubsystem::HandlePlayerInfo(const TArray<uint8>& Data)
{
	if (!FClientPacketCodec::HasExactSize(Data, SC_PLAYER_INFO_PACKET_SIZE)) return;
	const auto& Info = *reinterpret_cast<const SC_PlayerInfo*>(Data.GetData());
	if (Info.level < 1 || Info.userName[0] == '\0' || !std::memchr(Info.userName, '\0', sizeof(Info.userName))) return;
	if (Info.playerId == myId && Info.lifeId < PlayerLifeId) return;
	if (const uint32* Life = PlayerLives.Find(Info.playerId); Life && Info.lifeId < *Life) return;
	if (const FPlayerDisplayInfo* Previous = PlayerDisplayInfos.Find(Info.playerId); Previous && Info.lifeId < Previous->LifeId) return;
	const FString Name = FClientPacketCodec::DecodeFixedUtf8(Info.userName, sizeof(Info.userName)).TrimStartAndEnd();
	if (Name.IsEmpty()) return;
	// Identity can arrive before the first pose; do not create actors or advance pose generations here.
	PlayerDisplayInfos.Add(Info.playerId, {Name, Info.level, Info.lifeId});
}

void UNetSubsystem::HandlePlayerState(const TArray<uint8>& Data)
{
	if (Data.Num() != SC_PLAYER_STATE_PACKET_SIZE) return;
	const auto& State = *reinterpret_cast<const SC_PlayerState*>(Data.GetData());
	if (State.playerId != myId || State.lifeId < PlayerLifeId || State.maxHP <= 0 || State.hp < 0 || State.hp > State.maxHP ||
		State.maxMP <= 0 || State.mp < 0 || State.mp > State.maxMP || State.level < 1 || State.exp < 0 ||
		State.expToNext <= 0 || State.gold < 0 || !FMath::IsFinite(State.moveSpeed) || State.moveSpeed <= 0.f) return;
	for (const ItemSlot& Item : State.items)
		if ((Item.itemId == 0) != (Item.count == 0)) return;
	// 골드·경험치는 처치 보상으로만 오른다. 직전 상태와의 차이를 시스템 로그에 찍는다.
	TArray<FString> Rewards;
	if (bHasPlayerState)
	{
		int32 Exp = State.exp - PlayerState.exp;
		for (int32 Level = PlayerState.level; Level < State.level; ++Level)   // GameplayRules::RewardKill 의 필요 경험치 공식
			Exp += Level == PlayerState.level ? PlayerState.expToNext : 100 + (Level - 1) * 50;
		const int32 Gold = State.gold - PlayerState.gold;
		if (Exp > 0 && Gold > 0) Rewards.Add(FString::Printf(TEXT("경험치 +%d, 골드 +%d"), Exp, Gold));
		else if (Exp > 0) Rewards.Add(FString::Printf(TEXT("경험치 +%d"), Exp));
		else if (Gold > 0) Rewards.Add(FString::Printf(TEXT("골드 +%d"), Gold));
		if (State.level > PlayerState.level) Rewards.Add(FString::Printf(TEXT("레벨 업! Lv.%d"), State.level));
	}
	PlayerState = State;
	PlayerLifeId = State.lifeId;
	bHasPlayerState = true;
	PlayerStateTime = FPlatformTime::Seconds();
	if (ATurtleAgent* Agent = FindAgentById(myId))
	{
		ApplyHealthState(Agent, State.hp, State.maxHP);
		if (UPlayerStatsComponent* Stats = Agent->FindComponentByClass<UPlayerStatsComponent>())
		{
			Stats->MP = State.mp; Stats->MaxMP = State.maxMP; Stats->Attack = State.attack; Stats->Defense = State.defense;
			Stats->ApplyServerStats(State.level, State.exp, State.expToNext, State.gold);
		}
		Agent->GetCharacterMovement()->MaxWalkSpeed = State.moveSpeed;
	}
	OnGameplayStateChanged.Broadcast();
	for (const FString& Reward : Rewards) OnActionFeedback.Broadcast(Reward);
}

void UNetSubsystem::HandleActionResult(const TArray<uint8>& Data)
{
	if (Data.Num() != SC_ACTION_RESULT_PACKET_SIZE) return;
	const auto& Result = *reinterpret_cast<const SC_ActionResult*>(Data.GetData());
	FString Message;
	if (Result.requestId == static_cast<uint16>(PACKET_ID::CS_SHOP_BUY))
	{
		const FString Item = GetShopItemName(Result.actionId);
		switch (Result.result)
		{
		case ActionResult::Ok: Message = FString::Printf(TEXT("%s 구매 완료"), *Item); break;
		case ActionResult::NotEnoughGold: Message = TEXT("골드가 부족합니다."); break;
		case ActionResult::InventoryFull: Message = TEXT("인벤토리에 빈 칸이 없습니다."); break;
		case ActionResult::PriceChanged: Message = TEXT("가격이 바뀌었습니다. 목록을 확인하고 다시 눌러주세요."); break;
		case ActionResult::Cooldown: Message = TEXT("상인의 답변을 기다려주세요."); break;
		case ActionResult::Dead: Message = TEXT("사망 상태에서는 구매할 수 없습니다."); break;
		default: Message = TEXT("지금은 구매할 수 없습니다."); break;
		}
		OnShopFeedback.Broadcast(Message);
		return;
	}
	switch (Result.result)
	{
	case ActionResult::Ok: return;
	case ActionResult::Dead: Message = TEXT("사망 상태에서는 사용할 수 없습니다."); break;
	case ActionResult::Cooldown: Message = TEXT("아직 재사용 대기 중입니다."); break;
	case ActionResult::NotEnoughMP: Message = TEXT("MP가 부족합니다."); break;
	case ActionResult::NoEffect: Message = TEXT("이미 최대치입니다."); break;
	case ActionResult::StaleInventory: Message = TEXT("아이템 상태를 갱신했습니다. 다시 시도해주세요."); break;
	default: Message = TEXT("사용할 수 없는 행동입니다."); break;
	}
	OnActionFeedback.Broadcast(Message);
}

void UNetSubsystem::UseItem(int32 Slot)
{
	if (IsSinglePlay() || !IsConnected() || !bHasPlayerState || Slot < 0 || Slot >= INVENTORY_SLOT_COUNT) return;
	// Equipment works while held; only potions are used from a slot.
	const uint16 SlotItem = PlayerState.items[Slot].itemId;
	if (SlotItem != GameplayRules::HealthPotion && SlotItem != GameplayRules::ManaPotion) return;
	CS_UseItem Packet{};
	Packet.lifeId = PlayerLifeId;
	Packet.size = sizeof(Packet); Packet.id = static_cast<uint16>(PACKET_ID::CS_USE_ITEM);
	Packet.slot = static_cast<uint8>(Slot); Packet.expectedItemId = PlayerState.items[Slot].itemId;
	Packet.inventoryRevision = PlayerState.inventoryRevision;
	SendPacket(Packet);
}

void UNetSubsystem::BuyItem(int32 ItemId)
{
	const ShopProduct* Product = FindShopProduct(ItemId);
	if (IsSinglePlay() || !IsConnected() || !bHasPlayerState || !Product) return;
	CS_ShopBuy Packet{};
	Packet.size = sizeof(Packet); Packet.id = static_cast<uint16>(PACKET_ID::CS_SHOP_BUY);
	Packet.lifeId = PlayerLifeId; Packet.itemId = Product->itemId; Packet.quantity = 1;
	Packet.unitPrice = Product->unitPrice;
	SendPacket(Packet);
}

const ShopProduct* UNetSubsystem::FindShopProduct(int32 ItemId) const
{
	if (!bHasShopPrices || ItemId <= 0) return nullptr;
	for (int32 i = 0; i < ShopPrices.count; ++i)
		if (ShopPrices.products[i].itemId == ItemId) return &ShopPrices.products[i];
	return nullptr;
}

FString UNetSubsystem::GetShopItemName(int32 ItemId) const
{
	const ShopProduct* Product = FindShopProduct(ItemId);
	return Product ? FClientPacketCodec::DecodeFixedUtf8(Product->name, sizeof(Product->name)) : FString::Printf(TEXT("아이템 %d"), ItemId);
}

int32 UNetSubsystem::ItemIdFromIcon(const FString& TextureName)
{
	if (TextureName.Contains(TEXT("PotionRed"))) return GameplayRules::HealthPotion;
	if (TextureName.Contains(TEXT("PotionBlue"))) return GameplayRules::ManaPotion;
	static const FString Prefix = TEXT("T_UI_ShopItem_");
	if (!TextureName.StartsWith(Prefix)) return 0;
	FString Id = TextureName.Mid(Prefix.Len());
	Id.Split(TEXT("_"), &Id, nullptr);
	return Id.IsNumeric() ? FCString::Atoi(*Id) : 0;
}

void UNetSubsystem::HandleShopPrices(const TArray<uint8>& Data)
{
	if (Data.Num() != SC_SHOP_PRICES_PACKET_SIZE) return;
	const auto& Prices = *reinterpret_cast<const SC_ShopPrices*>(Data.GetData());
	if (Prices.affinity > 100 || Prices.count > SHOP_MAX_PRODUCTS) return;
	for (int32 i = 0; i < Prices.count; ++i)
		if (Prices.products[i].itemId == 0 || Prices.products[i].unitPrice <= 0) return;
	ShopPrices = Prices;
	bHasShopPrices = true;
	OnShopPricesChanged.Broadcast();
}

void UNetSubsystem::HandleRoomLeaveNotify(const TArray<uint8>& RecvData)
{
	if (!FClientPacketCodec::HasExactSize(RecvData, SC_ROOM_LEAVE_NOTIFY_PACKET_SIZE))
	{
		return;
	}

	const SC_RoomLeaveNotify* Pkt = reinterpret_cast<const SC_RoomLeaveNotify*>(RecvData.GetData());
	PlayerLives.Remove(Pkt->userIndex);
	PlayerDisplayInfos.Remove(Pkt->userIndex);
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
		ResetNpcWorld();
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

void UNetSubsystem::NotifyIngameReady(float X, float Y, float Z, float Yaw)
{
	if (IsSinglePlay() || !IsConnected())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UNpcSubsystem* Npcs = World->GetSubsystem<UNpcSubsystem>())
		{
			// Each new world starts unready. The server resends its snapshot after this request.
			Npcs->SetReady(true);
		}
	}
	CS_IngameReady Pkt{};
	Pkt.size = CS_INGAME_READY_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_INGAME_READY);
	Pkt.x = X;
	Pkt.y = Y;
	Pkt.yaw = Yaw;
	Pkt.z = Z;
	Pkt.protocolVersion = GAMEPLAY_PROTOCOL_VERSION;
	SendPacket(Pkt);
	UE_LOG(LogTemp, Log, TEXT("[Net] 인게임 준비 완료 통지"));
}

void UNetSubsystem::EnterLobby()
{
	ResetNpcWorld();
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
	ResetNpcWorld();
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

void UNetSubsystem::ResetNpcWorld()
{
	const bool bHadState = bHasPlayerState || !players.IsEmpty();
	bHasPlayerState = false;
	PlayerState = {};
	bHasShopPrices = false;
	PlayerLifeId = 0;
	PlayerLives.Reset();
	PlayerDisplayInfos.Reset();
	for (auto& Entry : players) if (IsValid(Entry.Value)) Entry.Value->Destroy();
	players.Reset();
	if (bHadState) OnGameplayStateChanged.Broadcast();
	if (UWorld* World = GetWorld())
	{
		if (UNpcSubsystem* Npcs = World->GetSubsystem<UNpcSubsystem>())
		{
			Npcs->SetReady(false);
		}
	}
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

bool UNetSubsystem::GetPlayerDisplayInfo(const ATurtleAgent* Agent, FString& OutName, int32& OutLevel) const
{
	OutName.Reset(); OutLevel = 0;
	if (!IsValid(Agent)) return false;
	uint32 Id = myId;
	const bool bLocal = Agent == FindAgentById(myId);
	if (!bLocal)
	{
		bool bFound = false;
		for (const auto& Entry : players)
			if (Entry.Value == Agent) { Id = Entry.Key; bFound = true; break; }
		if (!bFound) return false;
	}
	if (const FPlayerDisplayInfo* Info = PlayerDisplayInfos.Find(Id))
	{
		OutName = Info->Name; OutLevel = Info->Level;
	}
	else OutName = bLocal ? StoredUserID : FindUserNameByIndex(Id);
	if (OutName.IsEmpty()) OutName = TEXT("Player");
	if (bLocal)
	{
		if (!IsSinglePlay() && bHasPlayerState) OutLevel = PlayerState.level;
		else if (IsSinglePlay())
			if (const UPlayerStatsComponent* Stats = Agent->FindComponentByClass<UPlayerStatsComponent>()) OutLevel = Stats->Level;
	}
	return true;
}

void UNetSubsystem::SendAttack(int32 SkillId, int64 TargetId, float Yaw)
{
	if (IsSinglePlay() || !IsConnected())
	{
		return;
	}

	CS_Attack Pkt{};
	Pkt.lifeId = PlayerLifeId;
	Pkt.size = CS_ATTACK_PACKET_SIZE;
	Pkt.id = static_cast<uint16>(PACKET_ID::CS_ATTACK);
	Pkt.skillId = static_cast<uint16>(SkillId);
	Pkt.targetId = static_cast<uint32>(TargetId);
	Pkt.targetKind = TargetId >= 0 ? ENTITY_NPC : ENTITY_NONE;
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
