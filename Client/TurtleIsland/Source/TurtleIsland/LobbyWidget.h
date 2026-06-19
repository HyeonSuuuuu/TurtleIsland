// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "LobbyWidget.generated.h"

/**
 * 
 */
UCLASS()
class TURTLEISLAND_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;
	
	UPROPERTY(meta = (BindWidget))
	class UButton* RoomButton;

	UFUNCTION()
	void OnRoomButtonClicked();
};
