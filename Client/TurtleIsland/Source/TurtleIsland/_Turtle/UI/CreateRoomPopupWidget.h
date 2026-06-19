// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "CreateRoomPopupWidget.generated.h"

UCLASS()
class TURTLEISLAND_API UCreateRoomPopupWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UEditableText* ET_RoomName;

	UPROPERTY(meta = (BindWidget))
	UButton* RoomButton;

	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Cancel;

	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Close;

	UFUNCTION()
	void OnCreateClicked();

	UFUNCTION()
	void OnCloseClicked();
};
