// Fill out your copyright notice in the Description page of Project Settings.

#include "ChatLogWidget.h"

#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "_Turtle/Network/NetSubsystem.h"
#include "TurtleIsland.h"

UNetSubsystem* UChatLogWidget::GetNet() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UNetSubsystem>() : nullptr;
}

void UChatLogWidget::CacheLineStyle()
{
	if (bStyleCached)
	{
		return;
	}

	// 1순위: 디자이너에 남아 있는 줄에서 베낀다 (디자이너에서 고치면 바로 반영된다)
	if (MessageContainer)
	{
		for (UWidget* Child : MessageContainer->GetAllChildren())
		{
			if (UTextBlock* AsText = Cast<UTextBlock>(Child))
			{
				CachedFont = AsText->GetFont();
				CachedColour = AsText->GetColorAndOpacity();
				bStyleCached = true;
				return;
			}
		}
	}

	// 2순위: 위젯 설정에 지정한 폰트
	if (LineFont.HasValidFont())
	{
		CachedFont = LineFont;
		CachedColour = LineColour;
		bStyleCached = true;
		return;
	}

	UE_LOG(LogTurtleIsland, Warning,
		TEXT("[Chat] 줄 폰트를 못 정했다. MessageContainer 에 TextBlock 을 하나 두거나 "
			 "위젯 기본값의 Line Font 를 지정해야 한글이 안 깨진다."));
}

void UChatLogWidget::NativeConstruct()
{
	// 스타일 견본을 먼저 챙기고 컨테이너를 비운다. Super 안에서 블루프린트의
	// Event Construct 가 돌기 때문에, 그 전에 해둬야 디자이너 줄이 남아 있다.
	CacheLineStyle();
	if (MessageContainer)
	{
		MessageContainer->ClearChildren();
	}
	BuiltLineCount = 0;

	Super::NativeConstruct();

	if (ChatInput)
	{
		// NativeConstruct 는 재진입할 수 있으므로 두 번 붙지 않게 먼저 뗀다
		ChatInput->OnTextCommitted.RemoveDynamic(this, &UChatLogWidget::HandleTextCommitted);
		ChatInput->OnTextCommitted.AddDynamic(this, &UChatLogWidget::HandleTextCommitted);
	}

	if (ChatSendBtn)
	{
		ChatSendBtn->OnClicked.RemoveDynamic(this, &UChatLogWidget::HandleSendClicked);
		ChatSendBtn->OnClicked.AddDynamic(this, &UChatLogWidget::HandleSendClicked);
	}

	if (UNetSubsystem* Net = GetNet())
	{
		Net->OnChatMessage.RemoveDynamic(this, &UChatLogWidget::HandleChatMessage);
		Net->OnChatMessage.AddDynamic(this, &UChatLogWidget::HandleChatMessage);
	}

	RefreshChatLines();

	UE_LOG(LogTurtleIsland, Log,
		TEXT("[Chat] 위젯 준비 - ChatInput=%s, Container=%s, Net=%s, 스타일견본=%s"),
		ChatInput ? TEXT("O") : TEXT("X"),
		MessageContainer ? TEXT("O") : TEXT("X"),
		GetNet() ? TEXT("O") : TEXT("X"),
		bStyleCached ? TEXT("O") : TEXT("X"));
}

void UChatLogWidget::NativeDestruct()
{
	if (ChatInput)
	{
		ChatInput->OnTextCommitted.RemoveDynamic(this, &UChatLogWidget::HandleTextCommitted);
	}
	if (ChatSendBtn)
	{
		ChatSendBtn->OnClicked.RemoveDynamic(this, &UChatLogWidget::HandleSendClicked);
	}
	if (UNetSubsystem* Net = GetNet())
	{
		Net->OnChatMessage.RemoveDynamic(this, &UChatLogWidget::HandleChatMessage);
	}

	Super::NativeDestruct();
}

void UChatLogWidget::ReturnFocusToGame()
{
	// 열 때와 닫을 때가 대칭이어야 한다. 한쪽만 입력 모드를 건드리면 상태가 어긋나
	// "나왔는데 WASD 가 안 되거나" "다시 들어갔는데 타이핑이 안 되는" 일이 생긴다.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void UChatLogWidget::FocusChatInput()
{
	if (!ChatInput)
	{
		return;
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		// GameOnly 상태에서는 뷰포트가 키보드를 붙들고 있어서, SetKeyboardFocus 를
		// 불러도 EditableText 까지 키가 안 온다. UI 가 입력을 받을 수 있는 모드로
		// 먼저 바꿔야 한다.
		//
		// SetWidgetToFocus 는 쓰지 않는다. UserWidget 을 넘기면 그 래퍼인
		// SObjectWidget 이 대상이 되는데 그건 포커스를 못 받아서
		// "Attempting to focus Non-Focusable widget" 에러가 난다.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);

		// GameAndUI 에서 커서를 숨기면 뷰포트가 마우스를 캡처하면서 키보드 포커스까지
		// 되가져간다. 커서를 띄워야 포커스가 붙어 있는다.
		PC->SetShowMouseCursor(bMouseCursorWhileChatting);
	}

	ChatInput->SetKeyboardFocus();
}

void UChatLogWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		const bool bWasEmpty = Text.ToString().TrimStartAndEnd().IsEmpty();

		// GetText() 를 다시 읽지 않고 넘어온 값을 쓴다. 블루프린트에서도 같은
		// OnTextCommitted 를 잡아 입력창을 먼저 비우면 GetText() 가 빈 문자열이 된다.
		SendChatText(Text.ToString());
		if (ChatInput)
		{
			ChatInput->SetText(FText::GetEmpty());
		}

		// 빈 채로 엔터 = "채팅 그만". ESC 말고도 나갈 길을 준다.
		// 뭔가 보냈으면 포커스를 유지해 연속으로 칠 수 있게 한다.
		if (bWasEmpty || !bKeepFocusAfterSend)
		{
			ReturnFocusToGame();
		}
		// 포커스는 건드리지 않는다. ChatInput 의 Clear Keyboard Focus On Commit 을
		// 꺼놨으므로 엔터로 확정해도 위젯이 포커스를 스스로 유지한다.
		// 여기서 SetKeyboardFocus 를 또 부르면 방금 확정한 상태를 헤집어서
		// 캐럿은 깜빡이는데 키가 안 들어가는 상태가 된다.
	}
	else if (CommitMethod == ETextCommit::OnCleared)
	{
		// ESC — 쓰던 건 버리고 게임으로 돌아간다
		if (ChatInput)
		{
			ChatInput->SetText(FText::GetEmpty());
		}
		ReturnFocusToGame();
	}
}

void UChatLogWidget::HandleSendClicked()
{
	SubmitChat();

	// 버튼을 누르면 포커스가 버튼으로 넘어간다. 입력창으로 돌려줘야 이어서 칠 수 있다.
	// 입력 모드는 건드리지 않는다 — 버튼을 눌렀다는 건 이미 커서가 떠 있다는 뜻이다.
	if (ChatInput)
	{
		ChatInput->SetKeyboardFocus();
	}
}

void UChatLogWidget::SubmitChat()
{
	if (!ChatInput)
	{
		return;
	}
	const FString Typed = ChatInput->GetText().ToString();
	ChatInput->SetText(FText::GetEmpty());
	SendChatText(Typed);
}

void UChatLogWidget::SendChatText(const FString& Message)
{
	if (Message.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	UNetSubsystem* Net = GetNet();
	if (!Net)
	{
		UE_LOG(LogTurtleIsland, Warning, TEXT("[Chat] NetSubsystem 을 못 찾음"));
		return;
	}

	// 싱글/미접속이면 SendChat 이 로컬에만 찍는다 — 그 분기는 서브시스템이 갖고 있다
	Net->SendChat(Message);
}

void UChatLogWidget::HandleChatMessage(FString Line)
{
	// 인자는 쓰지 않는다. 어떤 이유로 불렸든 배열 상태에 화면을 맞춘다.
	RefreshChatLines();
}

void UChatLogWidget::RefreshChatLines()
{
	if (!MessageContainer)
	{
		return;
	}

	UNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	const TArray<FString>& Lines = Net->ChatLines;

	// 새 줄을 붙이기 전에 "지금 맨 아래를 보고 있었나"를 기억해 둔다.
	// 위로 올려 지난 대화를 읽는 중이라면 따라 내려가면 안 된다.
	bool bWasAtBottom = true;
	if (ChatLogScrollBox && bFollowOnlyWhenAtBottom)
	{
		bWasAtBottom =
			ChatLogScrollBox->GetScrollOffset() >= ChatLogScrollBox->GetScrollOffsetOfEnd() - 1.f;
	}

	// 줄이 줄었으면(오래된 것이 잘려나갔으면) 통째로 다시 만든다.
	// 늘기만 했으면 늘어난 만큼만 붙인다 — 매번 다 지우고 만들면 스크롤 위치가 튄다.
	if (Lines.Num() < BuiltLineCount)
	{
		MessageContainer->ClearChildren();
		BuiltLineCount = 0;
	}

	for (int32 i = BuiltLineCount; i < Lines.Num(); ++i)
	{
		UTextBlock* Row = NewObject<UTextBlock>(this);
		if (!Row)
		{
			continue;
		}
		Row->SetText(FText::FromString(Lines[i]));
		if (bStyleCached)
		{
			// 디자이너에서 잡아둔 폰트·색 그대로. 안 그러면 새 줄만 기본 폰트로 튄다.
			Row->SetFont(CachedFont);
			Row->SetColorAndOpacity(CachedColour);
		}
		Row->SetAutoWrapText(true);
		MessageContainer->AddChildToVerticalBox(Row);
	}
	BuiltLineCount = Lines.Num();

	if (ChatLogScrollBox && bWasAtBottom)
	{
		ChatLogScrollBox->ScrollToEnd();
	}
}
