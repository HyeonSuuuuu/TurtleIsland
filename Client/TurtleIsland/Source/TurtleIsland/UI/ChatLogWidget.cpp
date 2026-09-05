// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ChatLogWidget.h"

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
#include "Network/NetSubsystem.h"
#include "TurtleIsland.h"

namespace
{
	/** 포커스 감시 주기(초). */
	constexpr float FocusWatchdogPeriod = 0.1f;

	/** 이만큼 포커스를 못 잡고 있으면 채팅이 끝난 것으로 본다. */
	constexpr float FocusLostGrace = 0.3f;
}

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

	// 채팅 중에 위젯이 사라지면 무시 카운터가 올라간 채로 남아 조작이 영영 막힌다.
	// 입력 모드는 건드리지 않는다 — 로비로 나가는 길이면 커서가 필요하다.
	if (bChatInputActive)
	{
		bChatInputActive = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(FocusWatchdogHandle);
		}
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->SetIgnoreLookInput(false);
			PC->SetIgnoreMoveInput(false);
		}
	}

	Super::NativeDestruct();
}

void UChatLogWidget::ReturnFocusToGame()
{
	APlayerController* PC = GetOwningPlayer();

	// SetIgnore*Input 은 누적 카운터다. 진입 때 올린 만큼만 정확히 내려야 한다 —
	// 짝이 안 맞으면 조작이 영영 막히거나, 채팅 중인데 시점이 돌아간다.
	if (bChatInputActive)
	{
		bChatInputActive = false;
		FocusLostSeconds = 0.f;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(FocusWatchdogHandle);
		}
		if (PC)
		{
			PC->SetIgnoreLookInput(false);
			PC->SetIgnoreMoveInput(false);
		}
	}

	// 열 때와 닫을 때가 대칭이어야 한다. 한쪽만 입력 모드를 건드리면 상태가 어긋나
	// "나왔는데 WASD 가 안 되거나" "다시 들어갔는데 타이핑이 안 되는" 일이 생긴다.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);

		// GameOnly 가 거는 포커스 이동·마우스 캡처는 FReply 로 쌓였다가 "다음 입력
		// 이벤트" 때 적용된다. 그때까지 뷰포트가 마우스를 안 잡아서 커서가 화면에
		// 그대로 남는다. 여기서 직접 넘겨야 커서가 바로 사라진다.
		FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
	}

	if (PC)
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

		// GameAndUI 는 키만 위젯으로 보내고 마우스 "이동"은 계속 게임으로 흘린다.
		// 그래서 커서를 움직이면 시점까지 같이 돌아간다. 채팅 동안은 아예 막는다.
		// (마우스 캡처로 막으면 커서가 UI 를 못 만지므로 컨트롤러 쪽에서 무시시킨다.)
		if (!bChatInputActive)
		{
			bChatInputActive = true;
			FocusLostSeconds = 0.f;
			PC->SetIgnoreLookInput(true);
			PC->SetIgnoreMoveInput(true);

			if (bExitChatOnFocusLoss)
			{
				if (UWorld* World = GetWorld())
				{
					World->GetTimerManager().SetTimer(
						FocusWatchdogHandle, this, &UChatLogWidget::WatchChatFocus,
						FocusWatchdogPeriod, true);
				}
			}
		}
	}

	ChatInput->SetKeyboardFocus();
}

void UChatLogWidget::WatchChatFocus()
{
	if (!bChatInputActive)
	{
		return;
	}

	// 입력창이 포커스를 놓쳤다 = 뷰포트를 클릭했거나 다른 UI 로 넘어갔다는 뜻.
	// 이걸 안 보면 아무도 ReturnFocusToGame 을 안 불러서 GameAndUI 로 남고,
	// 커서가 화면에 계속 떠 있게 된다.
	const bool bStillTyping =
		ChatInput &&
		(ChatInput->HasKeyboardFocus() || ChatInput->HasAnyUserFocus() ||
		 ChatInput->HasUserFocusedDescendants(GetOwningPlayer()));

	// 전송 버튼을 누른 직후처럼 한두 틱 비는 경우가 있어 유예를 둔다.
	FocusLostSeconds = bStillTyping ? 0.f : FocusLostSeconds + FocusWatchdogPeriod;
	if (FocusLostSeconds >= FocusLostGrace)
	{
		UE_LOG(LogTurtleIsland, Verbose, TEXT("[Chat] 입력창이 포커스를 잃어 채팅을 닫는다"));
		ReturnFocusToGame();
	}
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
		FocusLostSeconds = 0.f;   // 버튼으로 포커스가 잠깐 넘어간 걸 이탈로 세지 않는다
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
