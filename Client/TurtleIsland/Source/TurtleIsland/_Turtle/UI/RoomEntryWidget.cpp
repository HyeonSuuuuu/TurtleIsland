// Fill out your copyright notice in the Description page of Project Settings.

#include "RoomEntryWidget.h"
#include "NetSubsystem.h"

void URoomEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Btn_Join)
		Btn_Join->OnClicked.AddDynamic(this, &URoomEntryWidget::OnJoinClicked);
}

void URoomEntryWidget::InitWithRoomData(const FRoomData& Data)
{
	CachedRoomId = Data.RoomId;
	if (Text_RoomTitle)
		Text_RoomTitle->SetText(FText::FromString(Data.Title));
	if (Text_HostName)
		Text_HostName->SetText(FText::FromString(Data.HostName));
	if (Text_UserCount)
		Text_UserCount->SetText(FText::FromString(FString::Printf(TEXT("%d명"), Data.UserCount)));
}

void URoomEntryWidget::OnJoinClicked()
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
		Net->EnterRoom(CachedRoomId);
}
