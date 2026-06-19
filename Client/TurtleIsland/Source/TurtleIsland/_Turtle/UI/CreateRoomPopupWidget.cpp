// Fill out your copyright notice in the Description page of Project Settings.

#include "CreateRoomPopupWidget.h"
#include "NetSubsystem.h"

void UCreateRoomPopupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RoomButton)
		RoomButton->OnClicked.AddDynamic(this, &UCreateRoomPopupWidget::OnCreateClicked);
	if (Btn_Cancel)
		Btn_Cancel->OnClicked.AddDynamic(this, &UCreateRoomPopupWidget::OnCloseClicked);
	if (Btn_Close)
		Btn_Close->OnClicked.AddDynamic(this, &UCreateRoomPopupWidget::OnCloseClicked);
}

void UCreateRoomPopupWidget::OnCreateClicked()
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
	{
		FString Title = ET_RoomName ? ET_RoomName->GetText().ToString() : TEXT("");
		Net->CreateRoom(Title);
	}

	RemoveFromParent();
}

void UCreateRoomPopupWidget::OnCloseClicked()
{
	RemoveFromParent();
}
