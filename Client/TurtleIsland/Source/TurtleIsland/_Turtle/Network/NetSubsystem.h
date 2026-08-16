// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "OpponentCharacter.h"
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

class FNetworker;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginFailed, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginSucceeded, bool, bIsNewAccount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomEnterFailed, FString, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomListUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomMembersUpdated);

UCLASS()
class TURTLEISLAND_API UNetSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	UNetSubsystem() = default;
	virtual ~UNetSubsystem() = default;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void EnqueRecvPacket();
	void DequeRecvPacket();

	void EnqueSendPacket(TArray<uint8>&& packet);

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

	// 로비 입장(CS_LOBBY_ENTER 전송 + Lv1_Lobby 로드). 로그인 성공 후 호출.
	void EnterLobby();

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
	void NotifyIngameReady(float X, float Y, float Yaw);

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
	TSubclassOf<class AOpponentCharacter> OpponentClass;

	// 상대방을 배치할 아레나 바닥 높이(Z). 서버는 월드 2D(x,y)만 보내므로 Z는 고정값 사용.
	// Lv3_Ingame PlayerStart Z(=92)에 맞춤. 캐릭터가 바닥에 안 맞으면 여기만 조정.
	UPROPERTY(EditAnywhere, Category = "Network")
	float OpponentGroundZ = 92.f;

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

	UPROPERTY(BlueprintReadOnly, Category = "Network")
	bool IsConnect = false;

	UPROPERTY(BlueprintReadOnly, Category = "PlayMode")
	EPlayMode PlayMode = EPlayMode::Multi;

	FString StoredServerIP = TEXT("127.0.0.1");
	FString StoredUserID;

	FNetworker* Networker = nullptr;
	FRunnableThread* Thread = nullptr;

	UPROPERTY()
	TMap<uint32, AOpponentCharacter*> players;

	uint32 myId = 0;
};
