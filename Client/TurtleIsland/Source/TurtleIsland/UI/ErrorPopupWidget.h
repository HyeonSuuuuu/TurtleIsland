// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "ErrorPopupWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPopupConfirmed);

UCLASS()
class TURTLEISLAND_API UErrorPopupWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// 확인 버튼 클릭 시 브로드캐스트(팝업은 스스로 닫힘)
	UPROPERTY(BlueprintAssignable)
	FOnPopupConfirmed OnConfirmed;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* TitleText;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* MessageText;

	UPROPERTY(meta = (BindWidget))
	UButton* ConfirmButton;

	UFUNCTION()
	void OnConfirmClicked();

public:
	void SetMessage(const FString& Title, const FString& Message);
};
