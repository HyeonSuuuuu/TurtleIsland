// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "ErrorPopupWidget.h"
#include "LoginPopupWidget.h"
#include "ServerSettingsWidget.h"
#include "LoginWidget.generated.h"

// 메인 메뉴. SINGLE / MULTI / OPTION / EXIT 네 개를 세로로 배치하고,
// MULTI를 누르면 ULoginPopupWidget을 띄워 ID/PW를 받는다.
UCLASS()
class TURTLEISLAND_API ULoginWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- 메뉴 버튼 (WBP_Login에 없어도 컴파일되도록 Optional) ---
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* SinglePlayButton;

	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* MultiPlayButton;

	// OPTION. 아직 미구현이라 클릭 동작은 연결하지 않는다.
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* OptionButton;

	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* ExitButton;

	// --- 팝업 클래스 (WBP에서 지정) ---
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<ULoginPopupWidget> LoginPopupClass;

	// WBP_ErrorPopup을 여기에 할당
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UErrorPopupWidget> ErrorPopupClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UServerSettingsWidget> SettingsPopupClass;

	UPROPERTY()
	ULoginPopupWidget* ActiveLoginPopup;

	UPROPERTY()
	UErrorPopupWidget* ActivePopup;

	UPROPERTY()
	UServerSettingsWidget* ActiveSettingsPopup;

	// --- 메뉴 동작 ---
	UFUNCTION()
	void OnMultiPlayButtonClicked();

	// 로그인 팝업의 톱니바퀴에서만 호출된다. 메인 메뉴 OPTION과는 무관.
	UFUNCTION()
	void OpenServerSettings();

	// 서버 접속 없이 바로 인게임으로
	UFUNCTION()
	void OnSinglePlayButtonClicked();

	UFUNCTION()
	void OnExitButtonClicked();

	// --- 로그인 팝업에서 올라오는 이벤트 ---
	UFUNCTION()
	void OnLoginSubmitted(FString ID, FString PW);

	UFUNCTION()
	void OnLoginCancelled();

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
	void CloseLoginPopup();

	// 팝업이 떠 있는 동안 메인 메뉴 버튼을 잠근다
	void SetMenuInteractable(bool bEnabled);

	FString PendingID;
	FString PendingPW;
	FTimerHandle LoginRetryHandle;
};
