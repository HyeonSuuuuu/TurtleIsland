// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/EditableText.h"
#include "RoomEntryWidget.h"
#include "ErrorPopupWidget.h"
#include "LobbyWidget.generated.h"

UCLASS()
class TURTLEISLAND_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_CreateRoom;

	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_Refresh;

	UPROPERTY(meta = (BindWidget))
	class UTextBlock* ID;

	UPROPERTY(meta = (BindWidget))
	class UScrollBox* ScrollBox_RoomList;

	UPROPERTY(meta = (BindWidgetOptional))
	class UEditableText* SearchInput;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<URoomEntryWidget> RoomEntryClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> CreateRoomPopupClass;

	// WBP_ErrorPopup 할당 (방 생성/입장 실패 안내용)
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UErrorPopupWidget> ErrorPopupClass;

	UPROPERTY()
	UUserWidget* ActiveCreateRoomPopup;

	UPROPERTY()
	UErrorPopupWidget* ActivePopup;

	UFUNCTION()
	void OnCreateRoomClicked();

	UFUNCTION()
	void OnRefreshClicked();

	UFUNCTION()
	void OnSearchTextChanged(const FText& Text);

	UFUNCTION()
	void OnRoomListUpdated();

	// 방 생성/입장 실패 안내 팝업
	UFUNCTION()
	void OnRoomEnterFailed(FString Message);

	void PopulateRoomList(const FString& Filter = TEXT(""));
};
