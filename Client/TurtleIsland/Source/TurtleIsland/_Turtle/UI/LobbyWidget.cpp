// Fill out your copyright notice in the Description page of Project Settings.

#include "LobbyWidget.h"
#include "NetSubsystem.h"
#include "Protocol.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Btn_CreateRoom)
		Btn_CreateRoom->OnClicked.AddDynamic(this, &ULobbyWidget::OnCreateRoomClicked);
	if (Btn_Refresh)
		Btn_Refresh->OnClicked.AddDynamic(this, &ULobbyWidget::OnRefreshClicked);
	if (SearchInput)
		SearchInput->OnTextChanged.AddDynamic(this, &ULobbyWidget::OnSearchTextChanged);

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
	{
		if (ID)
			ID->SetText(FText::FromString(Net->GetUserID()));

		Net->OnRoomListUpdated.AddDynamic(this, &ULobbyWidget::OnRoomListUpdated);
		PopulateRoomList();
	}
}

void ULobbyWidget::NativeDestruct()
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
		Net->OnRoomListUpdated.RemoveDynamic(this, &ULobbyWidget::OnRoomListUpdated);

	Super::NativeDestruct();
}

void ULobbyWidget::OnCreateRoomClicked()
{
	if (!CreateRoomPopupClass) return;

	if (ActiveCreateRoomPopup)
	{
		ActiveCreateRoomPopup->RemoveFromParent();
		ActiveCreateRoomPopup = nullptr;
	}

	ActiveCreateRoomPopup = CreateWidget<UUserWidget>(GetOwningPlayer(), CreateRoomPopupClass);
	if (ActiveCreateRoomPopup)
		ActiveCreateRoomPopup->AddToViewport(10);
}

void ULobbyWidget::OnRefreshClicked()
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (!Net) return;

	CS_LobbyEnter Pkt{};
	Pkt.size = CS_LOBBY_ENTER_PACKET_SIZE;
	Pkt.id   = static_cast<uint16>(PACKET_ID::CS_LOBBY_ENTER_REQUEST);
	TArray<uint8> Buf;
	Buf.SetNumUninitialized(CS_LOBBY_ENTER_PACKET_SIZE);
	FMemory::Memcpy(Buf.GetData(), &Pkt, CS_LOBBY_ENTER_PACKET_SIZE);
	Net->EnqueSendPacket(MoveTemp(Buf));
}

void ULobbyWidget::OnSearchTextChanged(const FText& Text)
{
	PopulateRoomList(Text.ToString());
}

void ULobbyWidget::OnRoomListUpdated()
{
	FString Filter = SearchInput ? SearchInput->GetText().ToString() : TEXT("");
	PopulateRoomList(Filter);
}

void ULobbyWidget::PopulateRoomList(const FString& Filter)
{
	if (!ScrollBox_RoomList || !RoomEntryClass) return;

	ScrollBox_RoomList->ClearChildren();

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (!Net) return;

	for (const FRoomData& Room : Net->RoomList)
	{
		if (!Filter.IsEmpty() && !Room.Title.Contains(Filter, ESearchCase::IgnoreCase))
			continue;

		URoomEntryWidget* Entry = CreateWidget<URoomEntryWidget>(GetOwningPlayer(), RoomEntryClass);
		if (Entry)
		{
			Entry->InitWithRoomData(Room);
			ScrollBox_RoomList->AddChild(Entry);
		}
	}
}
