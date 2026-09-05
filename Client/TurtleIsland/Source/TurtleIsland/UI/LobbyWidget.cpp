// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/LobbyWidget.h"
#include "Network/NetSubsystem.h"
#include "Protocol.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "GameFramework/PlayerController.h"

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 메뉴 화면이므로 마우스 커서를 표시하고 UI 입력 모드로 전환
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
	}

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
		Net->OnRoomEnterFailed.AddDynamic(this, &ULobbyWidget::OnRoomEnterFailed);
		PopulateRoomList();
	}
}

void ULobbyWidget::NativeDestruct()
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
	{
		Net->OnRoomListUpdated.RemoveDynamic(this, &ULobbyWidget::OnRoomListUpdated);
		Net->OnRoomEnterFailed.RemoveDynamic(this, &ULobbyWidget::OnRoomEnterFailed);
	}

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
	Net->SendPacket(Pkt);
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

void ULobbyWidget::OnRoomEnterFailed(FString Message)
{
	UE_LOG(LogTemp, Warning, TEXT("[Lobby] 방 생성/입장 실패: %s"), *Message);

	if (!ErrorPopupClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Lobby] ErrorPopupClass 미할당 — WBP_Lobby 를 열어 'UI > ErrorPopupClass' 에 WBP_ErrorPopup 을 지정하세요."));
		return;
	}

	if (ActivePopup)
	{
		ActivePopup->RemoveFromParent();
		ActivePopup = nullptr;
	}

	ActivePopup = CreateWidget<UErrorPopupWidget>(GetOwningPlayer(), ErrorPopupClass);
	if (ActivePopup)
	{
		ActivePopup->SetMessage(TEXT("알림"), Message);
		ActivePopup->AddToViewport(20);
	}
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
