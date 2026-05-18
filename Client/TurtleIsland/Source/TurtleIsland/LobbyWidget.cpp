// Fill out your copyright notice in the Description page of Project Settings.


#include "LobbyWidget.h"
#include "Network/NetSubsystem.h"

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();


	if (RoomButton)
	{
		RoomButton->OnClicked.AddDynamic(this, &ULobbyWidget::OnRoomButtonClicked);
	}
}

void ULobbyWidget::OnRoomButtonClicked()
{
	UNetSubsystem* NetSubsystem = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (!NetSubsystem) return;

	// 방 목록이 있으면 첫 번째 방, 없으면 0번 방 (새로 생성됨)
	int32 RoomIndex = 0;
	if (NetSubsystem->RoomList.Num() > 0)
	{
		RoomIndex = NetSubsystem->RoomList[0].RoomId;
		UE_LOG(LogTemp, Log, TEXT("[Lobby] 기존 방 입장: RoomId=%d"), RoomIndex);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[Lobby] 방 없음 - 새 방 생성 (roomIndex=0)"));
	}

	NetSubsystem->EnterRoom(RoomIndex);
}

