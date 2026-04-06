// Fill out your copyright notice in the Description page of Project Settings.


#include "LobbyWidget.h"
#include "Network/NetSubsystem.h"
#include "Protocol.h"

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
	if (NetSubsystem)
	{
		CS_RoomStart RoomPacket{};
		RoomPacket.size = CS_ROOM_START_PACKET_SIZE;
		RoomPacket.id = (uint16)PACKET_ID::CS_ROOM_START_REQUEST;

		TArray<uint8> SendBuffer;
		SendBuffer.SetNumUninitialized(CS_ROOM_START_PACKET_SIZE);
		FMemory::Memcpy(SendBuffer.GetData(), &RoomPacket, CS_ROOM_START_PACKET_SIZE);
		NetSubsystem->EnqueSendPacket(MoveTemp(SendBuffer));
	}
}

