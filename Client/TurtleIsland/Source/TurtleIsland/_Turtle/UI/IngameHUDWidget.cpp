// Fill out your copyright notice in the Description page of Project Settings.

#include "IngameHUDWidget.h"

#include "Components/EditableText.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"

void UIngameHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ChatInput)
	{
		// 이미 붙어 있으면 두 번 붙지 않게 (NativeConstruct는 재진입할 수 있다)
		ChatInput->OnTextCommitted.RemoveDynamic(this, &UIngameHUDWidget::HandleTextCommitted);
		ChatInput->OnTextCommitted.AddDynamic(this, &UIngameHUDWidget::HandleTextCommitted);
	}

	CacheLineStyleFromPlaceholder();
}

void UIngameHUDWidget::NativeDestruct()
{
	if (ChatInput)
	{
		ChatInput->OnTextCommitted.RemoveDynamic(this, &UIngameHUDWidget::HandleTextCommitted);
	}

	Super::NativeDestruct();
}

void UIngameHUDWidget::FocusChat()
{
	if (bChatFocused || !ChatInput)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	// GameAndUI + SetWidgetToFocus: 포커스를 가진 EditableText가 키 입력을 소비하므로
	// 타이핑하는 동안 WASD가 캐릭터로 새지 않는다. UIOnly로 하면 마우스 시점까지
	// 죽어서 채팅 치는 동안 화면을 못 돌린다.
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(ChatInput->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);

	// 전투 중에 커서가 튀어나오면 거슬리므로 계속 숨겨 둔다.
	PC->SetShowMouseCursor(false);

	ChatInput->SetKeyboardFocus();
	bChatFocused = true;
}

void UIngameHUDWidget::CloseChat(bool bSubmit)
{
	if (!bChatFocused)
	{
		return;
	}

	if (ChatInput)
	{
		if (bSubmit)
		{
			const FText Typed = ChatInput->GetText();
			if (!Typed.IsEmpty())
			{
				// TODO: 서버 전송. 지금은 로컬 로그에만 올린다.
				AppendChatLine(Typed);
			}
		}
		ChatInput->SetText(FText::GetEmpty());
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}

	bChatFocused = false;
}

void UIngameHUDWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	switch (CommitMethod)
	{
	case ETextCommit::OnEnter:
		CloseChat(true);
		break;

	case ETextCommit::OnCleared:		// ESC
		CloseChat(false);
		break;

	default:
		// OnUserMovedFocus 등 — 다른 데를 클릭했으면 조용히 닫는다.
		CloseChat(false);
		break;
	}
}

FReply UIngameHUDWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bChatFocused && InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseChat(false);
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UIngameHUDWidget::CacheLineStyleFromPlaceholder()
{
	if (bLineStyleCached || !MessageContainer)
	{
		return;
	}

	for (UWidget* Child : MessageContainer->GetAllChildren())
	{
		if (UTextBlock* AsText = Cast<UTextBlock>(Child))
		{
			CachedLineFont = AsText->GetFont();
			CachedLineColour = AsText->GetColorAndOpacity();
			bLineStyleCached = true;
			return;
		}
	}
}

void UIngameHUDWidget::AppendChatLine(const FText& Line)
{
	if (!MessageContainer)
	{
		return;
	}

	CacheLineStyleFromPlaceholder();

	UTextBlock* NewLine = NewObject<UTextBlock>(this);
	if (!NewLine)
	{
		return;
	}

	NewLine->SetText(Line);
	if (bLineStyleCached)
	{
		// 자리표시자 줄과 같은 폰트·색이어야 섞였을 때 튀지 않는다.
		NewLine->SetFont(CachedLineFont);
		NewLine->SetColorAndOpacity(CachedLineColour);
	}
	MessageContainer->AddChildToVerticalBox(NewLine);

	// 오래된 줄부터 정리. 자리표시자도 같이 밀려 나가면서 자연스럽게 실데이터로 바뀐다.
	while (MessageContainer->GetChildrenCount() > MaxChatLines)
	{
		MessageContainer->RemoveChildAt(0);
	}

	if (ChatLogScrollBox)
	{
		ChatLogScrollBox->ScrollToEnd();
	}
}
