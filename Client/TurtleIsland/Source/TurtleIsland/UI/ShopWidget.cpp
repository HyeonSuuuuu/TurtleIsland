// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
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
#include "Engine/Texture2D.h"
#include "Network/NetSubsystem.h"
#include "GameplayRules.h"
#include "UI/ItemTooltipWidget.h"
#include "Styling/CoreStyle.h"
#include "TurtleIsland.h"

UShopItemButton::UShopItemButton() { InitIsFocusable(false); }

void UShopItemButton::Setup(int32 InItemId)
{
	ItemId = InItemId;
	OnClicked.AddUniqueDynamic(this, &UShopItemButton::Buy);
	const auto Tint = [](float Alpha)
	{
		FSlateBrush Brush;
		Brush.DrawAs = Alpha > 0.f ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::NoDrawType;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(6.f, 6.f, 6.f, 6.f);
		Brush.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, Alpha));
		return Brush;
	};
	FButtonStyle Style;
	Style.Normal = Tint(0.f); Style.Disabled = Tint(0.f);
	Style.Hovered = Tint(0.08f); Style.Pressed = Tint(0.16f);
	Style.NormalPadding = FMargin(0); Style.PressedPadding = FMargin(0);
	SetStyle(Style);
}

void UShopItemButton::Buy()
{
	if (UGameInstance* GI = GetGameInstance())
		if (UNetSubsystem* Net = GI->GetSubsystem<UNetSubsystem>()) Net->BuyItem(ItemId);
}

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

void UShopWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Slate 트리가 만들어지기 전에 칸을 버튼으로 감싼다. ReplaceChildAt 은 에디터 전용이라
	// 부모의 자식을 순서대로 다시 붙이고 슬롯 레이아웃(여백·정렬 등)을 복사한다.
	TMap<UPanelWidget*, TMap<UWidget*, UShopItemButton*>> WrapsByParent;
	TArray<UWidget*> Frames;
	WidgetTree->ForEachWidget([&Frames](UWidget* Widget) { if (Widget->GetName().StartsWith(TEXT("Frame"))) Frames.Add(Widget); });
	TSet<UWidget*> Wrapped;
	for (UWidget* Frame : Frames)
	{
		// Frame 이 배경 이미지 한 장이면 그 부모 칸을 상품으로 본다.
		UWidget* Product = Cast<UPanelWidget>(Frame) ? Frame : Frame->GetParent();
		if (!Product || Wrapped.Contains(Product)) continue;
		int32 ItemId = 0, ItemIcons = 0;
		UTextBlock* Price = nullptr;
		UTexture2D* IconTexture = nullptr;
		UWidgetTree::ForWidgetAndChildren(Product, [&](UWidget* Child)
		{
			if (const UImage* Icon = Cast<UImage>(Child))
				if (UObject* Texture = Icon->GetBrush().GetResourceObject())
					if (const int32 Id = UNetSubsystem::ItemIdFromIcon(Texture->GetName()))
					{
						ItemId = Id; ++ItemIcons; IconTexture = Cast<UTexture2D>(Texture);
					}
			if (UTextBlock* Text = Cast<UTextBlock>(Child)) Price = Text;
		});
		UPanelWidget* Parent = Product->GetParent();
		if (ItemIcons != 1 || !Parent) continue;
		UShopItemButton* Button = WidgetTree->ConstructWidget<UShopItemButton>();
		Button->Setup(ItemId);
		WrapsByParent.FindOrAdd(Parent).Add(Product, Button);
		Product->SetVisibility(ESlateVisibility::HitTestInvisible);
		Wrapped.Add(Product);
		ProductButtons.Add(Button);
		ProductPrices.Add(Price);
		ProductPriceColours.Add(Price ? Price->GetColorAndOpacity() : FSlateColor());
		UItemTooltipWidget* Tooltip = CreateWidget<UItemTooltipWidget>(this);
		Button->SetToolTip(Tooltip);
		ProductTooltips.Add(Tooltip);
		ProductIcons.Add(IconTexture);
		if (UNetSubsystem* Net = GetNet()) Net->RegisterItemIcon(ItemId, IconTexture); // HUD 인벤토리 칸도 같은 아이콘을 쓴다
	}
	for (const auto& [Parent, Wraps] : WrapsByParent)
	{
		const TArray<UPanelSlot*> OldSlots = Parent->GetSlots();
		TArray<UWidget*> Children;
		for (const UPanelSlot* OldSlot : OldSlots) Children.Add(OldSlot->Content);
		Parent->ClearChildren(); // old slot objects stay alive for the copy below
		for (int32 i = 0; i < OldSlots.Num(); ++i)
		{
			UShopItemButton* const* Button = Wraps.Find(Children[i]);
			UPanelSlot* NewSlot = Parent->AddChild(Button ? *Button : Children[i]);
			if (Button) (*Button)->AddChild(Children[i]);
			if (!NewSlot || NewSlot->GetClass() != OldSlots[i]->GetClass()) continue;
			for (TFieldIterator<FProperty> It(NewSlot->GetClass()); It; ++It)
				if (It->GetOwnerClass() != UPanelSlot::StaticClass() && !It->HasAnyPropertyFlags(CPF_Transient))
					It->CopyCompleteValue_InContainer(NewSlot, OldSlots[i]);
		}
	}
	UE_LOG(LogTurtleIsland, Log, TEXT("[Shop] 상품 칸 %d개를 구매 버튼으로 연결"), ProductButtons.Num());
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
		Net->OnShopPricesChanged.AddUniqueDynamic(this, &UShopWidget::RefreshProducts);
		Net->OnGameplayStateChanged.AddUniqueDynamic(this, &UShopWidget::RefreshProducts);
		Net->OnShopFeedback.AddUniqueDynamic(this, &UShopWidget::HandleShopFeedback);
	}
	RefreshProducts();

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
		Net->OnShopPricesChanged.RemoveDynamic(this, &UShopWidget::RefreshProducts);
		Net->OnGameplayStateChanged.RemoveDynamic(this, &UShopWidget::RefreshProducts);
		Net->OnShopFeedback.RemoveDynamic(this, &UShopWidget::HandleShopFeedback);
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
	FocusInput();
	// 처음 만든 위젯은 아직 레이아웃 전이라 포커스가 안 붙고, 컨트롤러가 이 뒤에
	// 입력 모드를 바꾸며 포커스를 가져가기도 한다. 다음 틱에 한 번 더 준다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UShopWidget::FocusInput);
	}
	RefreshProducts();
	if (ShownAffinity >= 0)
	{
		AddSystemLine(FString::Printf(TEXT("상인 호감도 %d / 100 (높을수록 더 깎아준다)"), ShownAffinity));
	}

	UE_LOG(LogTurtleIsland, Log, TEXT("[Shop] 대화 시작 (npcIndex=%d)"), NpcIndex);
}

void UShopWidget::FocusInput()
{
	if (Input_Message)
	{
		Input_Message->SetKeyboardFocus();
	}
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

	if (!Net->TrySendNpcChat(NpcIndex, Trimmed))
	{
		return;
	}

	// 서버는 보낸 사람에게 에코를 돌려주지 않는다. 큐 접수에 성공했을 때만 내 말풍선을 찍는다.
	AppendMessage(Trimmed, false);
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
			const FString NpcLabel = ShownAffinity >= 0
				? FString::Printf(TEXT("%s (호감도 %d)"), *NpcDisplayName, ShownAffinity) : NpcDisplayName;
			NameLabel->SetText(FText::FromString(bFromNpc ? NpcLabel : GetSelfDisplayName()));
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

void UShopWidget::RefreshProducts()
{
	UNetSubsystem* Net = GetNet();
	const SC_ShopPrices* Prices = Net ? Net->GetShopPrices() : nullptr;
	const SC_PlayerState* State = Net ? Net->GetPlayerState() : nullptr;
	if (Prices && Prices->affinity != ShownAffinity)
	{
		if (ShownAffinity >= 0)
		{
			const int32 Delta = Prices->affinity - ShownAffinity;
			AddSystemLine(FString::Printf(TEXT("상인 호감도 %d → %d (%s%d)"), ShownAffinity, Prices->affinity, Delta > 0 ? TEXT("+") : TEXT(""), Delta));
		}
		ShownAffinity = Prices->affinity;
	}
	for (int32 i = 0; i < ProductButtons.Num(); ++i)
	{
		const int32 ItemId = ProductButtons[i]->GetItemId();
		const ShopProduct* Product = Net ? Net->FindShopProduct(ItemId) : nullptr;
		const int32 Price = Product ? Product->unitPrice : 0;
		const int32 Quote = Product ? Product->quoteQuantity : 0;
		if (UTextBlock* Text = ProductPrices[i])
		{
			Text->SetText(FText::FromString(!Product ? FString(TEXT("--G"))
				: Quote ? FString::Printf(TEXT("%dG x%d"), Price, Quote) : FString::Printf(TEXT("%dG"), Price)));
			const bool bAffordable = State && State->gold >= Price;
			Text->SetColorAndOpacity(!bAffordable && Product ? UnaffordablePriceColour : Quote ? QuotePriceColour : ProductPriceColours[i]);
		}
		ProductTooltips[i]->SetItem(Net, ItemId, ProductIcons[i], !Product ? FString(TEXT("상점 정보를 받는 중"))
			: Quote ? FString::Printf(TEXT("흥정 가격 %dG · %d개 남음\n클릭하면 1개 구매"), Price, Quote)
			: FString::Printf(TEXT("%dG · 클릭하면 1개 구매"), Price));
		// 비활성 버튼은 아이콘까지 어둡게 그려진다. 살 수 없어도 밝게 두고, 부족하면 서버가 "골드가 부족합니다"로 답한다.
		ProductButtons[i]->SetIsEnabled(Product != nullptr);
	}
}

void UShopWidget::HandleShopFeedback(FString Message)
{
	AddSystemLine(Message);
}

void UShopWidget::AddSystemLine(const FString& Text)
{
	UPanelWidget* Target = MessageContainer
		? static_cast<UPanelWidget*>(MessageContainer.Get())
		: static_cast<UPanelWidget*>(DialogueScrollBox.Get());
	UTextBlock* Line = Target ? NewObject<UTextBlock>(this) : nullptr;
	if (!Line)
	{
		return;
	}
	const bool bWasAtBottom = WasScrolledToBottom();
	Line->SetText(FText::FromString(Text));
	if (bStyleCached)
	{
		FSlateFontInfo Font = CachedFont;
		Font.Size = FMath::Max(10.f, CachedFont.Size * 0.85f);
		Line->SetFont(Font);
	}
	Line->SetColorAndOpacity(SpeakerNameColour);
	Line->SetAutoWrapText(true);
	UPanelSlot* LineSlot = Target->AddChild(Line);
	if (UVerticalBoxSlot* AsVBox = Cast<UVerticalBoxSlot>(LineSlot))
	{
		AsVBox->SetHorizontalAlignment(HAlign_Center);
		AsVBox->SetPadding(FMargin(4.f, 4.f, 4.f, 8.f));
	}
	else if (UScrollBoxSlot* AsScroll = Cast<UScrollBoxSlot>(LineSlot))
	{
		AsScroll->SetHorizontalAlignment(HAlign_Center);
		AsScroll->SetPadding(FMargin(4.f, 4.f, 4.f, 8.f));
	}
	// 안내 줄 다음 말풍선엔 이름표를 다시 붙인다.
	bHasLastSpeaker = false;
	if (DialogueScrollBox && bWasAtBottom)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &UShopWidget::ScrollToBottomNow);
		}
	}
}
