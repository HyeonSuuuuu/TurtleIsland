// Fill out your copyright notice in the Description page of Project Settings.

#include "ShopWidget.h"

#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "_Turtle/Network/NetSubsystem.h"
#include "TurtleIsland.h"

UNetSubsystem* UShopWidget::GetNet() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UNetSubsystem>() : nullptr;
}

void UShopWidget::CacheLineStyle()
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
				bStyleCached = true;
				return;
			}
		}
	}

	// 2순위: 위젯 설정에 지정한 폰트
	if (LineFont.HasValidFont())
	{
		CachedFont = LineFont;
		bStyleCached = true;
		return;
	}

	UE_LOG(LogTurtleIsland, Warning,
		TEXT("[Shop] 줄 폰트를 못 정했다. MessageContainer 에 TextBlock 을 하나 두거나 "
			 "위젯 기본값의 Line Font 를 지정해야 한글이 안 깨진다."));
}

void UShopWidget::NativeConstruct()
{
	// 스타일 견본을 먼저 챙기고 컨테이너를 비운다. Super 안에서 블루프린트의
	// Event Construct 가 돌기 때문에, 그 전에 해둬야 디자이너 줄이 남아 있다.
	CacheLineStyle();
	if (MessageContainer)
	{
		MessageContainer->ClearChildren();
	}

	Super::NativeConstruct();

	if (Input_Message)
	{
		// NativeConstruct 는 재진입할 수 있으므로 두 번 붙지 않게 먼저 뗀다
		Input_Message->OnTextCommitted.RemoveDynamic(this, &UShopWidget::HandleTextCommitted);
		Input_Message->OnTextCommitted.AddDynamic(this, &UShopWidget::HandleTextCommitted);
	}
	if (Btn_Send)
	{
		Btn_Send->OnClicked.RemoveDynamic(this, &UShopWidget::HandleSendClicked);
		Btn_Send->OnClicked.AddDynamic(this, &UShopWidget::HandleSendClicked);
	}
	if (UNetSubsystem* Net = GetNet())
	{
		Net->OnNpcChatReceived.RemoveDynamic(this, &UShopWidget::HandleNpcChat);
		Net->OnNpcChatReceived.AddDynamic(this, &UShopWidget::HandleNpcChat);
	}

	UE_LOG(LogTurtleIsland, Log,
		TEXT("[Shop] 위젯 준비 - Input=%s, Container=%s, ScrollBox=%s, Net=%s, 스타일견본=%s"),
		Input_Message ? TEXT("O") : TEXT("X"),
		MessageContainer ? TEXT("O") : TEXT("X"),
		DialogueScrollBox ? TEXT("O") : TEXT("X"),
		GetNet() ? TEXT("O") : TEXT("X"),
		bStyleCached ? TEXT("O") : TEXT("X"));
}

void UShopWidget::NativeDestruct()
{
	if (Input_Message)
	{
		Input_Message->OnTextCommitted.RemoveDynamic(this, &UShopWidget::HandleTextCommitted);
	}
	if (Btn_Send)
	{
		Btn_Send->OnClicked.RemoveDynamic(this, &UShopWidget::HandleSendClicked);
	}
	if (UNetSubsystem* Net = GetNet())
	{
		Net->OnNpcChatReceived.RemoveDynamic(this, &UShopWidget::HandleNpcChat);
	}

	Super::NativeDestruct();
}

void UShopWidget::OpenShop(int32 InNpcIndex)
{
	NpcIndex = InNpcIndex;

	// 보통은 BP_IngamePlayerController 가 이미 붙여 놓은 뒤에 불린다.
	// 직접 띄우는 경로(테스트 등)를 위해서만 붙인다.
	if (!IsInViewport())
	{
		AddToViewport(10);
	}

	// 입력모드·커서·시점 무시는 건드리지 않는다. 컨트롤러가 짝을 맞춰 관리하고 있어서
	// 여기서 같이 만지면 닫을 때 되돌리는 쪽과 어긋난다.
	if (Input_Message)
	{
		Input_Message->SetKeyboardFocus();
	}

	UE_LOG(LogTurtleIsland, Log, TEXT("[Shop] 대화 시작 (npcIndex=%d)"), NpcIndex);
}

void UShopWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		// GetText() 를 다시 읽지 않고 넘어온 값을 쓴다. 블루프린트에서도 같은
		// OnTextCommitted 를 잡아 입력창을 먼저 비우면 GetText() 가 빈 문자열이 된다.
		SendToNpc(Text.ToString());
		if (Input_Message)
		{
			Input_Message->SetText(FText::GetEmpty());
			// 상점은 계속 대화하는 화면이라 포커스를 유지한다.
			Input_Message->SetKeyboardFocus();
		}
	}
	else if (CommitMethod == ETextCommit::OnCleared)
	{
		// ESC — 쓰던 것만 버린다. 상점을 닫는 건 컨트롤러 몫이다.
		if (Input_Message)
		{
			Input_Message->SetText(FText::GetEmpty());
		}
	}
}

void UShopWidget::HandleSendClicked()
{
	SubmitMessage();

	// 버튼을 누르면 포커스가 버튼으로 넘어간다. 입력창으로 돌려줘야 이어서 칠 수 있다.
	if (Input_Message)
	{
		Input_Message->SetKeyboardFocus();
	}
}

void UShopWidget::SubmitMessage()
{
	if (!Input_Message)
	{
		return;
	}
	const FString Typed = Input_Message->GetText().ToString();
	Input_Message->SetText(FText::GetEmpty());
	SendToNpc(Typed);
}

void UShopWidget::SendToNpc(const FString& Message)
{
	const FString Trimmed = Message.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return;
	}

	UNetSubsystem* Net = GetNet();
	if (!Net)
	{
		UE_LOG(LogTurtleIsland, Warning, TEXT("[Shop] NetSubsystem 을 못 찾음"));
		return;
	}

	// 서버는 보낸 사람에게 에코를 돌려주지 않는다. 내가 친 줄은 여기서 찍는다.
	const FString MyName = Net->GetUserID().IsEmpty() ? TEXT("나") : Net->GetUserID();
	AppendLine(FString::Printf(TEXT("[%s] %s"), *MyName, *Trimmed), false);

	Net->SendNpcChat(NpcIndex, Trimmed);
}

void UShopWidget::HandleNpcChat(FString Say)
{
	AppendLine(FString::Printf(TEXT("[%s] %s"), *NpcDisplayName, *Say), true);
}

void UShopWidget::AppendLine(const FString& Line, bool bFromNpc)
{
	UPanelWidget* Target = MessageContainer
		? static_cast<UPanelWidget*>(MessageContainer.Get())
		: static_cast<UPanelWidget*>(DialogueScrollBox.Get());
	if (!Target)
	{
		UE_LOG(LogTurtleIsland, Warning, TEXT("[Shop] 대화 로그를 붙일 곳이 없다: %s"), *Line);
		return;
	}

	// 붙이기 전에 "지금 맨 아래를 보고 있었나"를 기억해 둔다.
	// 위로 올려 지난 대화를 읽는 중이라면 따라 내려가면 안 된다.
	bool bWasAtBottom = true;
	if (DialogueScrollBox && bFollowOnlyWhenAtBottom)
	{
		bWasAtBottom =
			DialogueScrollBox->GetScrollOffset() >= DialogueScrollBox->GetScrollOffsetOfEnd() - 1.f;
	}

	UTextBlock* Row = NewObject<UTextBlock>(this);
	if (!Row)
	{
		return;
	}
	Row->SetText(FText::FromString(Line));
	if (bStyleCached)
	{
		// 디자이너에서 잡아둔 폰트 그대로. 안 그러면 새 줄만 기본 폰트로 튄다.
		Row->SetFont(CachedFont);
	}
	Row->SetColorAndOpacity(bFromNpc ? NpcColour : LineColour);
	Row->SetAutoWrapText(true);
	Target->AddChild(Row);

	if (DialogueScrollBox && bWasAtBottom)
	{
		DialogueScrollBox->ScrollToEnd();
	}
}
