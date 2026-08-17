// Fill out your copyright notice in the Description page of Project Settings.

#include "PauseMenuWidget.h"
#include "NetSubsystem.h"
#include "TurtleIslandPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ResumeButton)
		ResumeButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnResumeClicked);
	if (TitleButton)
		TitleButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnTitleClicked);
	if (QuitButton)
		QuitButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnQuitClicked);

	// ESC로 닫으려면 키 입력을 받아야 한다
	SetIsFocusable(true);
	SetKeyboardFocus();
}

FReply UPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPauseMenuWidget::Close()
{
	// 일시정지 해제/입력모드 복구는 컨트롤러가 일괄 처리
	if (ATurtleIslandPlayerController* PC = Cast<ATurtleIslandPlayerController>(GetOwningPlayer()))
		PC->ClosePauseMenu();
	else
		RemoveFromParent();
}

void UPauseMenuWidget::OnResumeClicked()
{
	Close();
}

void UPauseMenuWidget::OnTitleClicked()
{
	// 레벨 전환 전에 입력모드/일시정지를 되돌린다
	Close();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UNetSubsystem* Net = GI->GetSubsystem<UNetSubsystem>())
			Net->ReturnToTitle();
	}
}

void UPauseMenuWidget::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
