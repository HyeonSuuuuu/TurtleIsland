// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "LoginPopupWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoginSubmitted, FString, ID, FString, PW);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoginCancelled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsRequested);

// 메인 메뉴에서 MULTI를 누르면 뜨는 로그인 입력 팝업.
// 입력만 받아 넘기고 검증/전송은 ULoginWidget이 한다.
UCLASS()
class TURTLEISLAND_API ULoginPopupWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UEditableText* UserIDInput;

	UPROPERTY(meta = (BindWidget))
	UEditableText* UserPWInput;

	UPROPERTY(meta = (BindWidget))
	UButton* LoginButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CancelButton;

	// 서버 IP 설정 팝업을 여는 톱니바퀴 버튼
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SettingsButton;

	UFUNCTION()
	void OnLoginClicked();

	UFUNCTION()
	void OnSettingsClicked();

	// ID/PW 칸 어느 쪽에서든 엔터를 누르면 로그인 시도
	UFUNCTION()
	void OnTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnCancelClicked();

public:
	UPROPERTY(BlueprintAssignable, Category = "Login")
	FOnLoginSubmitted OnLoginSubmitted;

	UPROPERTY(BlueprintAssignable, Category = "Login")
	FOnLoginCancelled OnCancelled;

	// 실제 팝업 생성은 ULoginWidget이 한다(서버 IP를 그쪽이 들고 있음)
	UPROPERTY(BlueprintAssignable, Category = "Login")
	FOnSettingsRequested OnSettingsRequested;
};
