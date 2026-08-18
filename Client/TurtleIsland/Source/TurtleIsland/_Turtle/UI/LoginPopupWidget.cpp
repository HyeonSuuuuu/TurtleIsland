// Fill out your copyright notice in the Description page of Project Settings.

#include "LoginPopupWidget.h"

void ULoginPopupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (LoginButton)
		LoginButton->OnClicked.AddDynamic(this, &ULoginPopupWidget::OnLoginClicked);
	if (CancelButton)
		CancelButton->OnClicked.AddDynamic(this, &ULoginPopupWidget::OnCancelClicked);
	if (SettingsButton)
		SettingsButton->OnClicked.AddDynamic(this, &ULoginPopupWidget::OnSettingsClicked);

	if (UserIDInput)
	{
		UserIDInput->OnTextCommitted.AddDynamic(this, &ULoginPopupWidget::OnTextCommitted);
		// 열리자마자 바로 입력할 수 있게
		UserIDInput->SetKeyboardFocus();
	}
	if (UserPWInput)
		UserPWInput->OnTextCommitted.AddDynamic(this, &ULoginPopupWidget::OnTextCommitted);
}

void ULoginPopupWidget::OnLoginClicked()
{
	const FString ID = UserIDInput ? UserIDInput->GetText().ToString() : TEXT("");
	const FString PW = UserPWInput ? UserPWInput->GetText().ToString() : TEXT("");

	OnLoginSubmitted.Broadcast(ID, PW);
}

void ULoginPopupWidget::OnTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
		OnLoginClicked();
}

void ULoginPopupWidget::OnSettingsClicked()
{
	// 설정 팝업이 위에 뜰 뿐 로그인 팝업은 그대로 열어둔다
	OnSettingsRequested.Broadcast();
}

void ULoginPopupWidget::OnCancelClicked()
{
	OnCancelled.Broadcast();
	RemoveFromParent();
}
