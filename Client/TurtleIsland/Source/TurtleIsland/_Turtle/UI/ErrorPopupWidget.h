// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "ErrorPopupWidget.generated.h"

UCLASS()
class TURTLEISLAND_API UErrorPopupWidget : public UUserWidget
{
	GENERATED_BODY()
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
