// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Network/ClientPacketCodec.h"
#include <type_traits>
#include "Character/ServerAgent.h"
#include "NetSubsystem.generated.h"

// 플레이 모드. Single이면 서버에 접속하지 않고 패킷도 전혀 주고받지 않는다.
UENUM(BlueprintType)
enum class EPlayMode : uint8
{
	Multi  UMETA(DisplayName = "멀티"),
	Single UMETA(DisplayName = "싱글")
};

USTRUCT(BlueprintType)
struct FRoomData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int32 RoomId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int32 UserCount = 0;
};

// 현재 입장한 방의 참가자 1명 정보 (RoomUI에서 슬롯으로 표시)
USTRUCT(BlueprintType)
struct FRoomMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString UserID;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int32 UserIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bIsHost = false;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bIsLocal = false;
};

class FClientTransport;
class UChatSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginFailed, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginSucceeded, bool, bIsNewAccount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomEnterFailed, FString, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomListUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomMembersUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatMessage, FString, Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNpcChatReceived, FString, Say);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameplayStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDamageNumber, FVector, Location, int32, Damage, uint8, Flags);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionFeedback, FString, Message);

UCLASS()
class TURTLEISLAND_API UNetSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	UNetSubsystem() = default;
	friend class FLoginReconnectTest;
	virtual ~UNetSubsystem() = default;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void EnqueSendPacket(TArray<uint8>&& packet);

	// 헤더의 size 만큼 그대로 바이트로 보낸다. size, id 는 호출 전에 채워둘 것.
	template<typename TPacket>
	void SendPacket(const TPacket& Pkt)
	{
		static_assert(std::is_base_of_v<PacketHeader, TPacket>, "PacketHeader 상속 필요");
		check(Pkt.size >= PACKET_HEADER_SIZE && Pkt.size <= MaxPacketBytes);

		TArray<uint8> Buf;
		Buf.SetNumUninitialized(Pkt.size);
		FMemory::Memcpy(Buf.GetData(), &Pkt, Pkt.size);
		EnqueSendPacket(MoveTemp(Buf));
	}

	// 특정 방 입장 요청 (LobbyWidget에서 호출)
	UFUNCTION(BlueprintCallable, Category = "Network")
	void EnterRoom(int32 RoomIndex);

	// 방 생성 요청 (제목 지정, CreateRoomPopup에서 호출)
	UFUNCTION(BlueprintCallable, Category = "Network")
	void CreateRoom(const FString& Title);

	// 방 나가기 요청 (RoomUI '나가기' 버튼) → 서버 통보 후 로비로 복귀
	UFUNCTION(BlueprintCallable, Category = "Network")
	void LeaveRoom();

	// 방 시작 요청 (RoomUI '시작' 버튼, 방장 전용) → 서버가 SC_ROOM_START_NOTIFY로 응답
	UFUNCTION(BlueprintCallable, Category = "Network")
	void StartRoom();

	UFUNCTION(BlueprintCallable, Category = "Network")
	void RequestLogin(const FString& UserID, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "Network")
	void RefreshRooms();

	// 로비 입장(CS_LOBBY_ENTER 전송 + Lv1_Lobby 로드). 로그인 성공 후 호출.
	void EnterLobby();

	// ---- 채팅 ----
	// 인게임에서도 서버상 유저 상태가 Room 이라 방 채팅 패킷을 그대로 재사용한다.

	// 채팅 전송. 싱글 모드에서는 소켓을 타지 않고 로컬에만 표시한다.
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void SendChat(const FString& Message);

	// 최근 채팅 줄 (오래된 것 → 최신 순)
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	TArray<FString> ChatLines;

	// 새 줄이 추가되거나 오래된 줄이 사라질 때마다 브로드캐스트
	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FOnChatMessage OnChatMessage;

	// ChatLines 를 개행으로 이어붙인 문자열 (TextBlock 한 개에 바로 넣을 때)
	UFUNCTION(BlueprintPure, Category = "Chat")
	FString GetChatText() const;

	// 기록에 남길 최대 줄 수. 스크롤로 거슬러 올라갈 수 있는 범위다.
	UPROPERTY(EditAnywhere, Category = "Chat")
	int32 MaxChatLines = 100;

	// 줄이 사라지기까지의 시간(초). 0 이하면 사라지지 않는다.
	// 채팅창이 스크롤 방식이라 시간으로 지우면 지난 대화를 못 본다. 그래서 끈다.
	// 화면 위에 잠깐 떴다 사라지는 알림이 필요하면 그건 토스트로 따로 만드는 게 맞다.
	UPROPERTY(EditAnywhere, Category = "Chat")
	float ChatLifeSec = 0.f;

	// ---- NPC(상점) 대화 ----
	// 클라 CS_NPC_CHAT → 게임서버가 SA_AI 로 AI 서버에 전달 → AS_AI 응답을
	// 게임서버가 SC_NPC_CHAT 으로 중계한다. 방 채팅과 달리 텍스트가 고정 버퍼가
	// 아니라 헤더 뒤에 UTF-8 로 가변 길이로 붙는다(널 종단 없음, 길이는 size 로).

	// NPC 에게 말 걸기. 서버 유저 상태가 Room 일 때만 서버가 받아준다(= 인게임 포함).
	UFUNCTION(BlueprintCallable, Category = "Npc")
	void SendNpcChat(int32 NpcIndex, const FString& Message);

	bool TrySendNpcChat(int32 NpcIndex, const FString& Message);

	// ---- 전투 ----
	// 클라는 판정하지 않는다. 의도만 보내고 모션조차 SC_ATTACK_NOTIFY 를 받은 뒤에 재생한다.

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SendAttack(int32 SkillId, int64 TargetId, float Yaw);
	UFUNCTION(BlueprintCallable, Category="Items")
	void UseItem(int32 Slot);
	const SC_PlayerState* GetPlayerState() const { return bHasPlayerState ? &PlayerState : nullptr; }
	uint32 GetPlayerLifeId() const { return PlayerLifeId; }
	double GetPlayerStateTime() const { return PlayerStateTime; }
	UPROPERTY(BlueprintAssignable, Category="Gameplay")
	FOnGameplayStateChanged OnGameplayStateChanged;
	UPROPERTY(BlueprintAssignable, Category="Gameplay")
	FOnDamageNumber OnDamageNumber;
	UPROPERTY(BlueprintAssignable, Category="Gameplay")
	FOnActionFeedback OnActionFeedback;

	// ---- 상점 ----
	// 1개 구매. 가격은 서버가 보낸 SC_SHOP_PRICES 값을 그대로 되돌려 보내고, 다르면 서버가 거절한다.
	UFUNCTION(BlueprintCallable, Category="Shop")
	void BuyItem(int32 ItemId);
	const SC_ShopPrices* GetShopPrices() const { return bHasShopPrices ? &ShopPrices : nullptr; }
	// Server catalog entry (price, quote, equipment stats, name). Null until SC_SHOP_PRICES arrives.
	const ShopProduct* FindShopProduct(int32 ItemId) const;
	FString GetShopItemName(int32 ItemId) const;
	// Potion icons (PotionRed/PotionBlue) and T_UI_ShopItem_<id>_* map to item ids; 0 otherwise.
	static int32 ItemIdFromIcon(const FString& TextureName);
	UPROPERTY(BlueprintAssignable, Category="Shop")
	FOnGameplayStateChanged OnShopPricesChanged;
	// 구매 결과 문구. 상점 창에 찍는다.
	UPROPERTY(BlueprintAssignable, Category="Shop")
	FOnActionFeedback OnShopFeedback;

	// 서버가 쓰는 id 로 Agent 를 찾는다. 내 id 면 로컬 폰, 아니면 원격 목록에서.
	// NPC ids overlap player ids; NPC lifecycle uses UNpcSubsystem instead.
	ATurtleAgent* FindAgentById(uint32 Id) const;
	ATurtleAgent* FindEntity(uint8 Kind, uint32 Id) const;
	bool GetPlayerDisplayInfo(const ATurtleAgent* Agent, FString& OutName, int32& OutLevel) const;

	// NPC 답변 수신. 서버가 중계한 텍스트 그대로 넘어온다.
	UPROPERTY(BlueprintAssignable, Category = "Npc")
	FOnNpcChatReceived OnNpcChatReceived;

	// 와이어 패킷 최대 크기. 서버 Define.h 의 MAX_SOCKBUF 와 같아야 한다.
	static constexpr int32 MaxPacketBytes = 1024;

	// ---- 플레이 모드 ----

	// 싱글 플레이 시작. 네트워크를 끊고 로그인/로비/방을 건너뛰어 바로 인게임으로 진입.
	UFUNCTION(BlueprintCallable, Category = "PlayMode")
	void StartSinglePlay();

	// 멀티 플레이 모드로 전환(로그인 화면 기본 상태). 실제 접속은 로그인 시 Reconnect가 담당.
	UFUNCTION(BlueprintCallable, Category = "PlayMode")
	void StartMultiPlay();

	UFUNCTION(BlueprintPure, Category = "PlayMode")
	bool IsSinglePlay() const { return PlayMode == EPlayMode::Single; }

	UFUNCTION(BlueprintPure, Category = "PlayMode")
	EPlayMode GetPlayMode() const { return PlayMode; }

	// 인게임에서 타이틀(로그인) 화면으로 복귀. 접속을 끊고 세션 상태를 비운다.
	UFUNCTION(BlueprintCallable, Category = "PlayMode")
	void ReturnToTitle();

	// 싱글 플레이로 들어갈 인게임 레벨 이름
	static const FName IngameLevelName;

	// 타이틀(로그인) 레벨 이름
	static const FName LoginLevelName;

	// 인게임 로딩 완료 통지(CS_INGAME_READY) + 실제 스폰 위치 → 서버가 다른 플레이어/NPC 상태 응답
	void NotifyIngameReady(float X, float Y, float Z, float Yaw);

	// 한 방에 입장 가능한 최대 인원
	static constexpr int32 MaxRoomMembers = 4;

	// helper
	void CopyStringToBuffer(char* Dest, int32 DestSize, const FString& Source);
	bool IsConnected() const;
	void Reconnect(const FString& IP);
	void SetServerIP(const FString& IP) { StoredServerIP = IP; }
	FString GetServerIP() const { return StoredServerIP; }
	void SetUserID(const FString& ID) { StoredUserID = ID; }
	FString GetUserID() const { return StoredUserID; }

	// 상대방 캐릭터 BP 클래스 끌어다 사용
	UPROPERTY(EditAnywhere, Category = "Network")
	TSubclassOf<class AServerAgent> OpponentClass;

	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnLoginFailed OnLoginFailed;

	// 로그인 성공 시 브로드캐스트. 인자: 이번에 신규 가입되었는지 여부
	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnLoginSucceeded OnLoginSucceeded;

	// 방 생성/입장 실패 시 브로드캐스트(제목 중복, 정원 초과 등). 인자: 안내 메시지
	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnRoomEnterFailed OnRoomEnterFailed;

	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnRoomListUpdated OnRoomListUpdated;

	// 현재 방 참가자 목록이 바뀔 때마다 브로드캐스트 (RoomUI 갱신용)
	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnRoomMembersUpdated OnRoomMembersUpdated;

	// 서버에서 받은 방 목록 (Blueprint에서 읽기 가능)
	UPROPERTY(BlueprintReadOnly, Category = "Network")
	TArray<FRoomData> RoomList;

	// 현재 입장해 있는 방의 참가자 목록 (본인 포함, 최대 MaxRoomMembers명)
	UPROPERTY(BlueprintReadOnly, Category = "Network")
	TArray<FRoomMember> RoomMembers;

	// 현재 입장한 방의 제목 / 방장 이름 (로비 방 목록에서 캐시)
	UPROPERTY(BlueprintReadOnly, Category = "Network")
	FString CurrentRoomTitle;

	UPROPERTY(BlueprintReadOnly, Category = "Network")
	FString CurrentRoomHost;

protected:
	// 소켓 스레드 생성/정리. 싱글 모드에서는 아예 만들지 않는다.
	void StartNetwork();
	void ShutdownNetwork();

	// 방/유저 등 접속 세션에 묶인 상태를 전부 비운다.
	void ResetSessionState();
	void ResetNpcWorld();

	// 채팅 줄 추가(캡 적용) + 브로드캐스트
	void EmitChat(const FString& Line);

	// 수명이 다한 줄 제거. 지운 게 있으면 브로드캐스트.
	void PruneChatLines();

	void ProcessIncomingPackets();
	void DispatchPacket(const TArray<uint8>& RecvData);
	void PollPendingLogin(float DeltaTime);
	void SendPendingLogin();
	void FailPendingLogin(const FString& Message);

	void HandleLoginResponse(const TArray<uint8>& RecvData);
	void HandleLobbyResponse(const TArray<uint8>& RecvData);
	void HandleRoomEnterResponse(const TArray<uint8>& RecvData);
	void HandleRoomEnterNotify(const TArray<uint8>& RecvData);
	void HandleRoomChatResponse(const TArray<uint8>& RecvData);
	void HandleRoomChatNotify(const TArray<uint8>& RecvData);
	void HandleMoveNotify(const TArray<uint8>& RecvData, bool bRespawn = false);
	void HandleAttackNotify(const TArray<uint8>& RecvData);
	void HandleDamageNotify(const TArray<uint8>& RecvData);
	void HandlePlayerState(const TArray<uint8>& RecvData);
	void HandleHealthState(const TArray<uint8>& Data);
	void HandlePlayerInfo(const TArray<uint8>& Data);
	void ApplyHealthState(ATurtleAgent* Agent, int32 HP, int32 MaxHP);
	void HandleActionResult(const TArray<uint8>& RecvData);
	void HandleRoomLeaveNotify(const TArray<uint8>& RecvData);
	void HandleRoomStartNotify(const TArray<uint8>& RecvData);
	void HandleNpcChat(const TArray<uint8>& RecvData);
	void HandleShopPrices(const TArray<uint8>& RecvData);
	void SyncLegacyChatLines();

	// userIndex(=서버 playerId) → 표시 이름. 못 찾으면 "유저 N".
	FString FindUserNameByIndex(int32 UserIndex) const;

	// ChatLines 와 같은 인덱스의 추가 시각(초)
	TArray<double> ChatTimes;

	UPROPERTY(BlueprintReadOnly, Category = "Network")
	bool IsConnect = false;

	UPROPERTY(BlueprintReadOnly, Category = "PlayMode")
	EPlayMode PlayMode = EPlayMode::Multi;

	FString StoredServerIP = TEXT("127.0.0.1");
	FString StoredUserID;

	FClientTransport* Networker = nullptr;
	FRunnableThread* Thread = nullptr;

	UPROPERTY()
	TObjectPtr<UChatSubsystem> ChatSubsystem = nullptr;

	FString PendingLoginID;
	FString PendingLoginPW;
	float PendingLoginElapsed = 0.f;
	float LoginConnectTimeoutSec = 10.f;
	bool bLoginPending = false;

	UPROPERTY()
	TMap<uint32, AServerAgent*> players;

	uint32 myId = 0;
	uint32 PlayerLifeId = 0;
	TMap<uint32, uint32> PlayerLives;
	struct FPlayerDisplayInfo { FString Name; int32 Level; uint32 LifeId; };
	TMap<uint32, FPlayerDisplayInfo> PlayerDisplayInfos;
	SC_PlayerState PlayerState{};
	bool bHasPlayerState = false;
	double PlayerStateTime = 0;
	SC_ShopPrices ShopPrices{};
	bool bHasShopPrices = false;
};
