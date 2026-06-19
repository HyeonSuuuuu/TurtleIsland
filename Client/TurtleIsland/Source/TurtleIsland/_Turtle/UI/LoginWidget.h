// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "ErrorPopupWidget.h"
#include "ServerSettingsWidget.h"
#include "LoginWidget.generated.h"

UCLASS()
class TURTLEISLAND_API ULoginWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	class UButton* LoginButton;

	UPROPERTY(meta = (BindWidget))
	class UButton* SettingsButton;

	UPROPERTY(meta = (BindWidget))
	class UEditableText* UserIDInput;

	UPROPERTY(meta = (BindWidget))
	class UEditableText* UserPWInput;

	// WBP_ErrorPopup을 여기에 할당
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UErrorPopupWidget> ErrorPopupClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UServerSettingsWidget> SettingsPopupClass;

	UPROPERTY()
	UErrorPopupWidget* ActivePopup;

	UPROPERTY()
	UServerSettingsWidget* ActiveSettingsPopup;

	UFUNCTION()
	void OnLoginButtonClicked();

	UFUNCTION()
	void OnSettingsButtonClicked();

	UFUNCTION()
	void OnServerIPSaved(FString NewIP);

	UFUNCTION()
	void ShowError(FString Message);

	void TrySendLogin();

	FString PendingID;
	FString PendingPW;
	FTimerHandle LoginRetryHandle;
};
