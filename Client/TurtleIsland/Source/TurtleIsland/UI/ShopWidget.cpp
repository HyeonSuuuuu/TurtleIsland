// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ShopWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
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
	TypingBubble = nullptr;
	bHasLastSpeaker = false;

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

	// 서버는 보낸 사람에게 에코를 돌려주지 않는다. 내 말풍선은 여기서 찍는다.
	AppendMessage(Trimmed, false);

	Net->SendNpcChat(NpcIndex, Trimmed);

	// CPU 추론이 3~4초라 아무 반응이 없으면 먹통처럼 보인다.
	ShowTyping();
}

void UShopWidget::HandleNpcChat(FString Say)
{
	HideTyping();
	AppendMessage(Say, true);
}

FString UShopWidget::GetSelfDisplayName() const
{
	UNetSubsystem* Net = GetNet();
	const FString Id = Net ? Net->GetUserID() : FString();
	return Id.IsEmpty() ? TEXT("나") : Id;
}

bool UShopWidget::WasScrolledToBottom() const
{
	if (!DialogueScrollBox || !bFollowOnlyWhenAtBottom)
	{
		return true;
	}
	return DialogueScrollBox->GetScrollOffset()
		>= DialogueScrollBox->GetScrollOffsetOfEnd() - FollowSlackPx;
}

void UShopWidget::ScrollToBottomNow()
{
	if (DialogueScrollBox)
	{
		DialogueScrollBox->ScrollToEnd();
	}
}

void UShopWidget::AppendMessage(const FString& Message, bool bFromNpc)
{
	AddBubble(Message, bFromNpc, false);
}

void UShopWidget::AddBubble(const FString& Text, bool bFromNpc, bool bIsTyping)
{
	UPanelWidget* Target = MessageContainer
		? static_cast<UPanelWidget*>(MessageContainer.Get())
		: static_cast<UPanelWidget*>(DialogueScrollBox.Get());
	if (!Target)
	{
		UE_LOG(LogTurtleIsland, Warning, TEXT("[Shop] 대화 로그를 붙일 곳이 없다: %s"), *Text);
		return;
	}

	const bool bWasAtBottom = WasScrolledToBottom();

	// 슬롯 정렬·여백을 한 곳에서 처리한다. VerticalBox 든 ScrollBox 든 같은 API 를 쓴다.
	auto PlaceInContainer =
		[Target, bFromNpc](UWidget* Child, const FMargin& Pad)
		{
			UPanelSlot* Slot = Target->AddChild(Child);
			const EHorizontalAlignment HAlign = bFromNpc ? HAlign_Left : HAlign_Right;
			if (UVerticalBoxSlot* AsVBox = Cast<UVerticalBoxSlot>(Slot))
			{
				AsVBox->SetHorizontalAlignment(HAlign);
				AsVBox->SetPadding(Pad);
			}
			else if (UScrollBoxSlot* AsScroll = Cast<UScrollBoxSlot>(Slot))
			{
				AsScroll->SetHorizontalAlignment(HAlign);
				AsScroll->SetPadding(Pad);
			}
		};

	// 이름표는 화자가 바뀔 때만 붙인다. 연속으로 말하면 묶여 보인다.
	// 대기 말풍선은 곧 지워지므로 이름표를 새로 만들지 않는다.
	const bool bSpeakerChanged = !bHasLastSpeaker || bLastWasNpc != bFromNpc;
	if (bSpeakerChanged && !bIsTyping)
	{
		if (UTextBlock* NameLabel = NewObject<UTextBlock>(this))
		{
			NameLabel->SetText(FText::FromString(bFromNpc ? NpcDisplayName : GetSelfDisplayName()));
			if (bStyleCached)
			{
				FSlateFontInfo NameFont = CachedFont;
				NameFont.Size = FMath::Max(10.f, CachedFont.Size * 0.8f);
				NameLabel->SetFont(NameFont);
			}
			NameLabel->SetColorAndOpacity(SpeakerNameColour);
			PlaceInContainer(NameLabel, FMargin(4.f, 8.f, 4.f, 2.f));
		}
	}

	// 말풍선 본체: SizeBox(최대 폭) > Border(둥근 바탕) > TextBlock(자동 줄바꿈)
	USizeBox* Limit = NewObject<USizeBox>(this);
	UBorder*  Bubble = NewObject<UBorder>(this);
	UTextBlock* Body = NewObject<UTextBlock>(this);
	if (!Limit || !Bubble || !Body)
	{
		return;
	}

	Body->SetText(FText::FromString(bIsTyping ? TEXT("• • •") : Text));
	if (bStyleCached)
	{
		Body->SetFont(CachedFont);
	}
	Body->SetColorAndOpacity(bFromNpc ? NpcTextColour : SelfTextColour);
	Body->SetAutoWrapText(true);

	// DrawAs=Box 는 ResourceObject 가 없으면 아무것도 안 그린다. 단색 채움은 RoundedBox.
	// 꼬리 쪽 모서리 하나만 작게 줘서 어느 쪽 말인지 보이게 한다.
	const float R = BubbleCornerRadius;
	const float Tail = FMath::Min(4.f, R);
	FSlateBrush BubbleBrush;
	BubbleBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	BubbleBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	BubbleBrush.OutlineSettings.CornerRadii = bFromNpc
		? FVector4(Tail, R, R, R)    // 왼쪽 말풍선 — 왼쪽 위가 각짐
		: FVector4(R, Tail, R, R);   // 오른쪽 말풍선 — 오른쪽 위가 각짐
	BubbleBrush.TintColor = FSlateColor(bFromNpc ? NpcBubbleColour : SelfBubbleColour);
	Bubble->SetBrush(BubbleBrush);

	// UBorder 는 패딩을 슬롯과 위젯 두 군데에 들고 있고 동기화되지 않는다.
	// Slate 가 실제로 그리는 건 위젯 쪽이라 여기를 설정해야 글자가 테두리를 안 뚫는다.
	Bubble->SetPadding(FMargin(14.f, 9.f, 14.f, 9.f));
	Bubble->SetBrushColor(FLinearColor::White);   // 브러시 틴트를 그대로 살린다
	Bubble->SetContent(Body);

	Limit->SetMaxDesiredWidth(BubbleMaxWidth);
	Limit->AddChild(Bubble);

	PlaceInContainer(Limit, FMargin(4.f, 0.f, 4.f, 6.f));

	if (bIsTyping)
	{
		TypingBubble = Limit;
	}
	else
	{
		bLastWasNpc = bFromNpc;
		bHasLastSpeaker = true;
	}

	// 지금 한 번, 다음 틱에 한 번 더 부른다. 방금 붙인 말풍선의 레이아웃이 아직
	// 반영되지 않아서, 지금 부른 것만으로는 "예전 끝"까지만 내려간다.
	if (DialogueScrollBox && bWasAtBottom)
	{
		DialogueScrollBox->ScrollToEnd();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &UShopWidget::ScrollToBottomNow);
		}
	}
}

void UShopWidget::ShowTyping()
{
	if (TypingBubble)
	{
		return;   // 이미 떠 있다
	}
	AddBubble(FString(), true, true);
}

void UShopWidget::HideTyping()
{
	if (!TypingBubble)
	{
		return;
	}
	TypingBubble->RemoveFromParent();
	TypingBubble = nullptr;
}
