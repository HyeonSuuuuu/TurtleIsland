// Fill out your copyright notice in the Description page of Project Settings.

#include "ErrorPopupWidget.h"

void UErrorPopupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ConfirmButton)
		ConfirmButton->OnClicked.AddDynamic(this, &UErrorPopupWidget::OnConfirmClicked);
}

void UErrorPopupWidget::SetMessage(const FString& Title, const FString& Message)
{
	if (TitleText)
		TitleText->SetText(FText::FromString(Title));
	if (MessageText)
		MessageText->SetText(FText::FromString(Message));
}

void UErrorPopupWidget::OnConfirmClicked()
{
	OnConfirmed.Broadcast();
	RemoveFromParent();
}
