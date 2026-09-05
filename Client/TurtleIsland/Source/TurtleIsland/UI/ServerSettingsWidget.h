// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "ServerSettingsWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerIPSaved, FString, NewIP);

UCLASS()
class TURTLEISLAND_API UServerSettingsWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UEditableText* IPInput;

	UPROPERTY(meta = (BindWidget))
	UButton* SaveButton;

	UPROPERTY(meta = (BindWidget))
	UButton* CancelButton;

	UFUNCTION()
	void OnSaveClicked();

	UFUNCTION()
	void OnCancelClicked();

public:
	void InitWithIP(const FString& CurrentIP);

	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnServerIPSaved OnIPSaved;
};
