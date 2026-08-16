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

	// 싱글 플레이 진입 버튼. WBP_Login에 없어도 컴파일되도록 Optional.
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* SinglePlayButton;

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

	// ID/PW 입력칸에서 엔터를 누르면 로그인 시도
	UFUNCTION()
	void OnLoginTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnSettingsButtonClicked();

	// 서버 접속 없이 바로 인게임으로
	UFUNCTION()
	void OnSinglePlayButtonClicked();

	UFUNCTION()
	void OnServerIPSaved(FString NewIP);

	UFUNCTION()
	void ShowError(FString Message);

	// 로그인 성공: 신규 가입이면 안내 팝업 → 확인 후 로비, 기존이면 바로 로비
	UFUNCTION()
	void OnLoginSucceeded(bool bIsNewAccount);

	UFUNCTION()
	void OnAccountCreatedConfirmed();

	void TrySendLogin();

	FString PendingID;
	FString PendingPW;
	FTimerHandle LoginRetryHandle;
};
