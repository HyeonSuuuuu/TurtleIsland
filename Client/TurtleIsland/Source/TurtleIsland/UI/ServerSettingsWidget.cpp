// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ServerSettingsWidget.h"

void UServerSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SaveButton)
		SaveButton->OnClicked.AddDynamic(this, &UServerSettingsWidget::OnSaveClicked);
	if (CancelButton)
		CancelButton->OnClicked.AddDynamic(this, &UServerSettingsWidget::OnCancelClicked);
}

void UServerSettingsWidget::InitWithIP(const FString& CurrentIP)
{
	if (IPInput)
		IPInput->SetText(FText::FromString(CurrentIP));
}

void UServerSettingsWidget::OnSaveClicked()
{
	if (IPInput)
	{
		FString IP = IPInput->GetText().ToString().TrimStartAndEnd();
		if (!IP.IsEmpty())
			OnIPSaved.Broadcast(IP);
	}
	RemoveFromParent();
}

void UServerSettingsWidget::OnCancelClicked()
{
	RemoveFromParent();
}
