// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Network/NetSubsystem.h"
#include "RoomEntryWidget.generated.h"

UCLASS()
class TURTLEISLAND_API URoomEntryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void InitWithRoomData(const FRoomData& Data);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_RoomTitle;

	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_HostName;

	UPROPERTY(meta = (BindWidget))
	class UTextBlock* Text_UserCount;

	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_Join;

	UFUNCTION()
	void OnJoinClicked();

private:
	int32 CachedRoomId = 0;
};
