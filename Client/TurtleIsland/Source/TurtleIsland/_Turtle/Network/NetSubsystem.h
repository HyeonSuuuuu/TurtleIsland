// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "OpponentCharacter.h"
#include "NetSubsystem.generated.h"

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

	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnLoginFailed OnLoginFailed;

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
	UPROPERTY(BlueprintReadOnly, Category = "Network")
	bool IsConnect = false;

	FString StoredServerIP = TEXT("127.0.0.1");
	FString StoredUserID;

	FNetworker* Networker = nullptr;
	FRunnableThread* Thread = nullptr;

	UPROPERTY()
	TMap<uint32, AOpponentCharacter*> players;

	uint32 myId = 0;
};
