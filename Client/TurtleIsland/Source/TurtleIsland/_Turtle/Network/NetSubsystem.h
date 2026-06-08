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
class FNetworker;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginFailed, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomListUpdated);

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

	// 서버에서 받은 방 목록 (Blueprint에서 읽기 가능)
	UPROPERTY(BlueprintReadOnly, Category = "Network")
	TArray<FRoomData> RoomList;

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
