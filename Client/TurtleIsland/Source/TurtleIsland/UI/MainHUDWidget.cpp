#include "UI/MainHUDWidget.h"
#include "UI/PlayerNameplate.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Character/TurtleAgent.h"
#include "Character/ServerMonster.h"
#include "Components/CapsuleComponent.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/NpcNameplateComponent.h"
#include "GameplayRules.h"
#include "Network/ClientPacketCodec.h"
#include "Network/NetSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/ItemTooltipWidget.h"
#include "UI/MinimapWidget.h"
#include "UI/ShopWidget.h"
#include "UObject/UObjectIterator.h"

UQuickSlotButton::UQuickSlotButton() { InitIsFocusable(false); }

void UQuickSlotButton::Setup(UNetSubsystem* InNet, int32 InSlot)
{
	Net = InNet; SlotIndex = InSlot;
	OnClicked.AddUniqueDynamic(this, &UQuickSlotButton::UseSlot);
	FButtonStyle Transparent;
	Transparent.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
	Transparent.Hovered.DrawAs = ESlateBrushDrawType::NoDrawType;
	Transparent.Pressed.DrawAs = ESlateBrushDrawType::NoDrawType;
	Transparent.Disabled.DrawAs = ESlateBrushDrawType::NoDrawType;
	Transparent.NormalPadding = FMargin(0); Transparent.PressedPadding = FMargin(0);
	SetStyle(Transparent);
}

void UQuickSlotButton::UseSlot() { if (Net) Net->UseItem(SlotIndex); }

void UMainHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!GetWidgetFromName(TEXT("GoldText")))
	{
		if (UHorizontalBox* Row = Cast<UHorizontalBox>(GetWidgetFromName(TEXT("StatsRow"))))
		{
			UTextBlock* Gold = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GoldText"));
			Gold->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
			Gold->SetColorAndOpacity(FLinearColor(1.f, 0.8f, 0.2f));
			Row->AddChildToHorizontalBox(Gold);
		}
	}
	for (int32 i = 0; i < 8; ++i) SetText(*FString::Printf(TEXT("CombatLine%d"), i), TEXT(""));
	BindNetwork(GetGameInstance() ? GetGameInstance()->GetSubsystem<UNetSubsystem>() : nullptr);
	if (!Minimap && GetOwningPlayer())
	{
		Minimap = CreateWidget<UMinimapWidget>(GetOwningPlayer(), UMinimapWidget::StaticClass());
		if (Minimap) Minimap->AddToViewport(1);
	}
}

void UMainHUDWidget::BindNetwork(UNetSubsystem* InNet)
{
	if (Net)
	{
		Net->OnGameplayStateChanged.RemoveDynamic(this, &UMainHUDWidget::RefreshState);
		Net->OnDamageNumber.RemoveDynamic(this, &UMainHUDWidget::AddDamage);
		Net->OnActionFeedback.RemoveDynamic(this, &UMainHUDWidget::ShowFeedback);
		Net->OnShopPricesChanged.RemoveDynamic(this, &UMainHUDWidget::RefreshState);
	}
	Net = InNet;
	if (Net)
	{
		Net->OnGameplayStateChanged.AddUniqueDynamic(this, &UMainHUDWidget::RefreshState);
		Net->OnDamageNumber.AddUniqueDynamic(this, &UMainHUDWidget::AddDamage);
		Net->OnActionFeedback.AddUniqueDynamic(this, &UMainHUDWidget::ShowFeedback);
		Net->OnShopPricesChanged.AddUniqueDynamic(this, &UMainHUDWidget::RefreshState);
	}
	SetupSlots();
	RefreshState();
}

void UMainHUDWidget::NativeDestruct()
{
	BindNetwork(nullptr);
	DamagePopups.Reset();
	if (Minimap) Minimap->RemoveFromParent();
	Minimap = nullptr;
	Super::NativeDestruct();
}

void UMainHUDWidget::SetupSlots()
{
	if (SlotButtons.Num()) { for (int32 i = 0; i < SlotButtons.Num(); ++i) SlotButtons[i]->Setup(Net, i); return; }
	UUniformGridPanel* Grid = Cast<UUniformGridPanel>(GetWidgetFromName(TEXT("InventoryGrid")));
	if (!Grid) return;
	const TCHAR* Names[] = {TEXT("InvPotionRed"), TEXT("InvPotionBlue"), TEXT("InvCrystalPurple"), TEXT("InvScroll"), TEXT("InvFoodRation"), TEXT("InvEmpty")};
	for (int32 i = 0; i < INVENTORY_SLOT_COUNT; ++i)
	{
		UOverlay* Overlay = Cast<UOverlay>(GetWidgetFromName(Names[i]));
		if (!Overlay) continue;
		UImage* Icon = Cast<UImage>(GetWidgetFromName(*FString::Printf(TEXT("%sIcon"), Names[i])));
		UTextBlock* Count = Cast<UTextBlock>(GetWidgetFromName(*FString::Printf(TEXT("%sCount"), Names[i])));
		if (Icon && i < 2 && Net) Net->RegisterItemIcon(i + 1, Cast<UTexture2D>(Icon->GetBrush().GetResourceObject()));
		if (!Icon)
		{
			Icon = WidgetTree->ConstructWidget<UImage>();
			UOverlaySlot* IconSlot = Overlay->AddChildToOverlay(Icon);
			IconSlot->SetPadding(FMargin(8.f));
		}
		if (!Count)
		{
			Count = WidgetTree->ConstructWidget<UTextBlock>();
			Count->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
			UOverlaySlot* CountSlot = Overlay->AddChildToOverlay(Count);
			CountSlot->SetHorizontalAlignment(HAlign_Right); CountSlot->SetVerticalAlignment(VAlign_Bottom);
			CountSlot->SetPadding(FMargin(3.f));
		}
		const UUniformGridSlot* OldSlot = Cast<UUniformGridSlot>(Overlay->Slot);
		const int32 Row = OldSlot ? OldSlot->GetRow() : i / 3;
		const int32 Column = OldSlot ? OldSlot->GetColumn() : i % 3;
		Grid->RemoveChild(Overlay);
		UQuickSlotButton* Button = WidgetTree->ConstructWidget<UQuickSlotButton>();
		Button->Setup(Net, i); Button->AddChild(Overlay);
		Grid->AddChildToUniformGrid(Button, Row, Column);
		Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		SlotButtons.Add(Button); SlotIcons.Add(Icon); SlotCounts.Add(Count);
		SlotTooltips.Add(CreateWidget<UItemTooltipWidget>(this));
		SetText(*FString::Printf(TEXT("%sKey"), Names[i]), FString::FromInt(i + 1));
	}
	const TCHAR* SkillNames[] = {TEXT("SkillLMB"), TEXT("SkillQ"), TEXT("SkillE")};
	for (const TCHAR* Name : SkillNames)
	{
		if (UOverlay* Overlay = Cast<UOverlay>(GetWidgetFromName(Name)))
		{
			UTextBlock* Cooldown = WidgetTree->ConstructWidget<UTextBlock>();
			Cooldown->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
			Cooldown->SetShadowOffset(FVector2D(1, 1)); Cooldown->SetShadowColorAndOpacity(FLinearColor::Black);
			UOverlaySlot* CooldownSlot = Overlay->AddChildToOverlay(Cooldown);
			CooldownSlot->SetHorizontalAlignment(HAlign_Center); CooldownSlot->SetVerticalAlignment(VAlign_Center);
			SkillCooldowns.Add(Cooldown);
		}
	}
}

void UMainHUDWidget::SetText(FName Name, const FString& Value)
{
	if (UTextBlock* Text = Cast<UTextBlock>(GetWidgetFromName(Name))) Text->SetText(FText::FromString(Value));
}

void UMainHUDWidget::SetBar(FName Name, float Value)
{
	if (UProgressBar* Bar = Cast<UProgressBar>(GetWidgetFromName(Name))) Bar->SetPercent(Value);
}

void UMainHUDWidget::RefreshState()
{
	const SC_PlayerState* State = Net ? Net->GetPlayerState() : nullptr;
	SetText(TEXT("HPText"), State ? FString::Printf(TEXT("%d / %d"), State->hp, State->maxHP) : TEXT("-- / --"));
	SetText(TEXT("MPText"), State ? FString::Printf(TEXT("%d / %d"), State->mp, State->maxMP) : TEXT("-- / --"));
	SetText(TEXT("LvText"), State ? FString::FromInt(State->level) : TEXT("--"));
	SetText(TEXT("GoldText"), State ? FString::Printf(TEXT("  %d G"), State->gold) : TEXT("  -- G"));
	SetText(TEXT("XPText"), State ? FString::Printf(TEXT("%d / %d XP"), State->exp, State->expToNext) : TEXT("-- XP"));
	SetText(TEXT("StatAtkText"), State ? FString::FromInt(State->attack) : TEXT("--"));
	SetText(TEXT("StatDefText"), State ? FString::FromInt(State->defense) : TEXT("--"));
	SetText(TEXT("StatSpdText"), State ? FString::Printf(TEXT("%.0f"), State->moveSpeed) : TEXT("--"));
	SetBar(TEXT("HPProgressBar"), State ? static_cast<float>(State->hp) / State->maxHP : 0.f);
	SetBar(TEXT("MPProgressBar"), State ? static_cast<float>(State->mp) / State->maxMP : 0.f);
	SetBar(TEXT("XPProgressBar"), State ? FMath::Clamp(static_cast<float>(State->exp) / State->expToNext, 0.f, 1.f) : 0.f);
	for (int32 i = 0; i < SlotButtons.Num(); ++i)
	{
		const ItemSlot Item = State ? State->items[i] : ItemSlot{};
		UTexture2D* Texture = Item.count && Net ? Net->FindItemIcon(Item.itemId) : nullptr;
		SlotIcons[i]->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (Texture) SlotIcons[i]->SetBrushFromTexture(Texture, false);
		SlotCounts[i]->SetText(Item.count > 1 || Item.itemId == GameplayRules::HealthPotion || Item.itemId == GameplayRules::ManaPotion
			? FText::AsNumber(Item.count) : FText::GetEmpty());
		const bool bPotion = Item.itemId == GameplayRules::HealthPotion || Item.itemId == GameplayRules::ManaPotion;
		if (Item.count) SlotTooltips[i]->SetItem(Net, Item.itemId, Texture, bPotion ? FString::Printf(TEXT("%d키 또는 클릭으로 사용"), i + 1) : FString());
		SlotButtons[i]->SetToolTip(Item.count ? SlotTooltips[i] : nullptr);
	}
	if (!State) DamagePopups.Reset();
}

void UMainHUDWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	// World-projected plates change even when no UMG property changes (camera movement).
	if (const TSharedPtr<SWidget> SlateWidget = GetCachedWidget()) SlateWidget->Invalidate(EInvalidateWidgetReason::Paint);
	const float Width = Geometry.GetLocalSize().X;
	if (!FMath::IsNearlyEqual(Width, LastLayoutWidth))
	{
		LastLayoutWidth = Width;
		const bool bStackLogs = Width < 1720.f;
		const float LogWidth = FMath::Min(480.f, FMath::Max(1.f, (Width - 60.f) / 2.f));
		const TCHAR* LogNames[] = {TEXT("ChatPanel"), TEXT("CombatPanel")};
		for (int32 i = 0; i < 2; ++i)
			if (UWidget* Panel = GetWidgetFromName(LogNames[i]))
				if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot))
				{
					PanelSlot->SetPosition(FVector2D(i == 0 ? 20.f : -20.f, bStackLogs ? -208.f : -20.f));
					PanelSlot->SetSize(FVector2D(LogWidth, 236.f));
				}
	}
	const double Now = FPlatformTime::Seconds();
	DamagePopups.RemoveAll([Now](const FDamagePopup& Popup) { return Now - Popup.Start > 1.2; });
	const SC_PlayerState* State = Net ? Net->GetPlayerState() : nullptr;
	const double ElapsedMs = State ? (Now - Net->GetPlayerStateTime()) * 1000.0 : 0;
	for (int32 i = 0; i < SkillCooldowns.Num(); ++i)
	{
		const int32 Seconds = State ? FMath::Max(0, FMath::CeilToInt((State->skillCooldownMs[i] - ElapsedMs) / 1000.0)) : 0;
		SkillCooldowns[i]->SetText(Seconds ? FText::AsNumber(Seconds) : FText::GetEmpty());
	}
	for (int32 i = 0; i < SlotButtons.Num(); ++i)
		SlotButtons[i]->SetIsEnabled(State && State->hp > 0 && State->items[i].count && State->itemCooldownMs <= ElapsedMs);
}

void UMainHUDWidget::AddDamage(FVector Location, int32 Damage, uint8 Flags)
{
	if (DamagePopups.Num() >= 64) DamagePopups.RemoveAt(0);
	DamagePopups.Add({Location + FVector(0, 0, 110), Damage, Flags, FPlatformTime::Seconds()});
}

void UMainHUDWidget::ShowFeedback(FString Message)
{
	CombatLines.Add(MoveTemp(Message));
	if (CombatLines.Num() > 8) CombatLines.RemoveAt(0);
	for (int32 i = 0; i < 8; ++i) SetText(*FString::Printf(TEXT("CombatLine%d"), i), CombatLines.IsValidIndex(i) ? CombatLines[i] : TEXT(""));
	ScrollCombatLogToEnd();
	// 줄바꿈된 새 줄은 다음 레이아웃에서야 높이가 잡혀 한 번 더 내린다.
	if (UWorld* World = GetWorld()) World->GetTimerManager().SetTimerForNextTick(this, &UMainHUDWidget::ScrollCombatLogToEnd);
}

void UMainHUDWidget::ScrollCombatLogToEnd()
{
	// 빈 CombatLine 도 높이를 차지하므로, 8줄이 다 차기 전에 끝으로 내리면 첫 줄이 위로 잘린다.
	for (UWidget* Widget = GetWidgetFromName(TEXT("CombatLine0")); Widget; Widget = Widget->GetParent())
		if (UScrollBox* Scroll = Cast<UScrollBox>(Widget))
		{
			if (CombatLines.Num() >= 8) Scroll->ScrollToEnd(); else Scroll->ScrollToStart();
			return;
		}
}

namespace
{
	void DrawShadowedText(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry,
		const PlayerNameplate::FTextLayout& Text, const FVector2D& Position, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(Elements, Layer, Geometry.ToPaintGeometry(Text.Size, FSlateLayoutTransform(Position + FVector2D(1, 1))),
			Text.Text, Text.Font, ESlateDrawEffect::None, FLinearColor::Black);
		FSlateDrawElement::MakeText(Elements, Layer + 1, Geometry.ToPaintGeometry(Text.Size, FSlateLayoutTransform(Position)),
			Text.Text, Text.Font, ESlateDrawEffect::None, Color);
	}
}

int32 UMainHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 TopLayer = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled) + 1;
	APlayerController* PC = GetOwningPlayer();
	if (!PC || !Net) return TopLayer;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const APawn* Viewer = PC->GetPawn();
	const FVector ViewOrigin = Viewer ? Viewer->GetActorLocation() : PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const double MaxDistanceSq = MonsterPlateMaxDistance > 0.f ? FMath::Square(static_cast<double>(MonsterPlateMaxDistance)) : TNumericLimits<double>::Max();
	for (TActorIterator<ATurtleAgent> It(GetWorld()); It; ++It)
	{
		if (It->IsHidden()) continue;
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		FString Name;
		int32 Level = 0;
		const bool bPlayer = Net->GetPlayerDisplayInfo(*It, Name, Level);
		if (!bPlayer && FVector::DistSquared(It->GetActorLocation(), ViewOrigin) > MaxDistanceSq) continue;
		const AServerMonster* Monster = Cast<AServerMonster>(*It);
		FVector2D Position;
		if (!Health || (!bPlayer && Health->IsDead()) || !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,
			It->GetActorLocation() + FVector(0, 0, It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20), Position, true)) continue;
		if (Position.X < 0 || Position.Y < 0 || Position.X > Geometry.GetLocalSize().X || Position.Y > Geometry.GetLocalSize().Y) continue;
		if (bPlayer || Monster)
		{
			const FString TitleText = bPlayer ? FString::Printf(TEXT("Lv. %s  %s"),
				Level > 0 ? *FString::FromInt(Level) : TEXT("?"), *Name) : Monster->MonsterDisplayName.ToString();
			const auto Title = PlayerNameplate::FitText(TitleText, PlayerNameplate::NameWidth, 14, 11);
			const auto HP = PlayerNameplate::FitText(FString::Printf(TEXT("%d / %d"), Health->CurrentHP, Health->MaxHP),
				PlayerNameplate::BarWidth - 8.f, 10, 8);
			const FVector2D BarSize(PlayerNameplate::BarWidth, PlayerNameplate::BarHeight);
			const FVector2D BarPosition = Position - FVector2D(BarSize.X / 2.f, BarSize.Y);
			const bool bOwner = *It == PC->GetPawn();
			const FLinearColor Color = !bPlayer ? FLinearColor(0.8f, 0.12f, 0.12f) :
				(bOwner ? FLinearColor(0.12f, 0.65f, 0.25f) : FLinearColor(0.08f, 0.48f, 0.7f));
			FSlateDrawElement::MakeBox(Elements, TopLayer, Geometry.ToPaintGeometry(BarSize, FSlateLayoutTransform(BarPosition)),
				White, ESlateDrawEffect::None, FLinearColor(0.015f, 0.015f, 0.015f, 0.95f));
			FSlateDrawElement::MakeBox(Elements, TopLayer + 1,
				Geometry.ToPaintGeometry(FVector2D((BarSize.X - 2) * Health->GetHPRatio(), BarSize.Y - 2), FSlateLayoutTransform(BarPosition + FVector2D(1, 1))),
				White, ESlateDrawEffect::None, Color);
			DrawShadowedText(Elements, TopLayer + 2, Geometry, Title, FVector2D(Position.X - Title.Size.X / 2.f, BarPosition.Y - Title.Size.Y - 3.f), FLinearColor::White);
			DrawShadowedText(Elements, TopLayer + 2, Geometry, HP, BarPosition + (BarSize - HP.Size) / 2.f, FLinearColor::White);
			continue;
		}
		Position -= FVector2D(36, 4);
		FSlateDrawElement::MakeBox(Elements, TopLayer, Geometry.ToPaintGeometry(FVector2D(72, 7), FSlateLayoutTransform(Position)), White,
			ESlateDrawEffect::None, FLinearColor(0.03f, 0.03f, 0.03f, 0.9f));
		FSlateDrawElement::MakeBox(Elements, TopLayer + 1, Geometry.ToPaintGeometry(FVector2D(70 * Health->GetHPRatio(), 5), FSlateLayoutTransform(Position + FVector2D(1, 1))),
			White, ESlateDrawEffect::None, FLinearColor(0.8f, 0.12f, 0.12f));
	}
	// 진행 중 퀘스트 패널 (docs/ui-hud-spec.md 색상, 선형값). 서버가 보낸 처치 수를 그대로 보여준다.
	if (const SC_QuestState* Quests = Net->GetQuestState(); Quests && Quests->count > 0)
	{
		const FLinearColor PanelColor(0.0033f, 0.0144f, 0.0185f, 0.9f), BorderColor(0.1413f, 0.1f, 0.1095f),
			AccentColor(0.1022f, 0.3968f, 0.3613f), TextColor(0.7011f, 0.7011f, 0.7011f), BarColor(0.4342f, 0.2542f, 0.0612f);
		const float PanelWidth = 260.f, RowHeight = 52.f;
		const FVector2D PanelPosition(Geometry.GetLocalSize().X - PanelWidth - 24.f, QuestPanelTop);
		const FVector2D PanelSize(PanelWidth, 34.f + RowHeight * Quests->count);
		const auto Box = [&](const FVector2D& At, const FVector2D& Size, const FLinearColor& Color, int32 BoxLayer)
		{
			FSlateDrawElement::MakeBox(Elements, BoxLayer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(At)), White, ESlateDrawEffect::None, Color);
		};
		Box(PanelPosition, PanelSize, BorderColor, TopLayer);
		Box(PanelPosition + FVector2D(2, 2), PanelSize - FVector2D(4, 4), PanelColor, TopLayer + 1);
		const auto Header = PlayerNameplate::FitText(TEXT("진행 중 퀘스트"), PanelWidth - 24.f, 11, 10);
		DrawShadowedText(Elements, TopLayer + 2, Geometry, Header, PanelPosition + FVector2D(12, 9), TextColor);
		for (int32 i = 0; i < Quests->count; ++i)
		{
			const QuestProgress& Quest = Quests->quests[i];
			const FVector2D Row = PanelPosition + FVector2D(12, 30 + RowHeight * i);
			const bool bReady = Quest.goal > 0 && Quest.progress >= Quest.goal;
			const auto Title = PlayerNameplate::FitText(FClientPacketCodec::DecodeFixedUtf8(Quest.title, QUEST_TITLE_BYTES), PanelWidth - 24.f, 13, 11);
			const auto Count = PlayerNameplate::FitText(bReady ? TEXT("촌장에게 보고") : *FString::Printf(TEXT("%d / %d"), Quest.progress, Quest.goal), 100.f, 11, 10);
			const auto Objective = PlayerNameplate::FitText(FClientPacketCodec::DecodeFixedUtf8(Quest.objective, QUEST_OBJECTIVE_BYTES) + TEXT(" 처치"),
				PanelWidth - 36.f - Count.Size.X, 11, 10);
			DrawShadowedText(Elements, TopLayer + 2, Geometry, Title, Row, AccentColor);
			DrawShadowedText(Elements, TopLayer + 2, Geometry, Objective, Row + FVector2D(0, 19), TextColor);
			DrawShadowedText(Elements, TopLayer + 2, Geometry, Count, Row + FVector2D(PanelWidth - 24.f - Count.Size.X, 19), bReady ? FLinearColor(1.f, 0.85f, 0.4f) : TextColor);
			const float Ratio = Quest.goal > 0 ? FMath::Clamp(static_cast<float>(Quest.progress) / Quest.goal, 0.f, 1.f) : 0.f;
			Box(Row + FVector2D(0, 38), FVector2D(PanelWidth - 24.f, 4), FLinearColor(0.02f, 0.05f, 0.06f), TopLayer + 2);
			Box(Row + FVector2D(0, 38), FVector2D((PanelWidth - 24.f) * Ratio, 4), BarColor, TopLayer + 3);
		}
	}

	// NPC 이름표. 상호작용 구체 안이면 그 위에 "F 대화하기" 를 붙이고, 상점 창이 열려 있으면 안내는 숨긴다.
	bool bShopOpen = false;
	for (TObjectIterator<UShopWidget> Shop; Shop && !bShopOpen; ++Shop) bShopOpen = Shop->GetWorld() == GetWorld() && Shop->IsInViewport();
	for (const TWeakObjectPtr<UNpcNameplateComponent>& Weak : UNpcNameplateComponent::GetAll())
	{
		const UNpcNameplateComponent* Plate = Weak.Get();
		FVector2D Position;
		if (!Plate || Plate->GetWorld() != GetWorld() || Plate->DisplayName.IsEmpty() ||
			FVector::DistSquared(Plate->GetLabelLocation(), ViewOrigin) > MaxDistanceSq ||
			!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Plate->GetLabelLocation(), Position, true)) continue;
		const auto Name = PlayerNameplate::FitText(Plate->DisplayName.ToString(), PlayerNameplate::NameWidth, 16, 12);
		const FVector2D NamePosition(Position.X - Name.Size.X / 2.f, Position.Y - Name.Size.Y);
		DrawShadowedText(Elements, TopLayer + 2, Geometry, Name, NamePosition, FLinearColor(1.f, 0.85f, 0.4f));
		if (bShopOpen || !Plate->IsPlayerInRange(Viewer)) continue;
		const auto Key = PlayerNameplate::FitText(TEXT("F"), 20.f, 13, 13);
		const auto Prompt = PlayerNameplate::FitText(Plate->PromptText.ToString(), PlayerNameplate::NameWidth, 13, 11);
		const FVector2D KeySize(22, 22);
		const FVector2D PillSize(KeySize.X + Prompt.Size.X + 18, KeySize.Y + 6);
		const FVector2D PillPosition(Position.X - PillSize.X / 2.f, NamePosition.Y - PillSize.Y - 4);
		const FVector2D KeyPosition = PillPosition + FVector2D(3, 3);
		FSlateDrawElement::MakeBox(Elements, TopLayer, Geometry.ToPaintGeometry(PillSize, FSlateLayoutTransform(PillPosition)),
			White, ESlateDrawEffect::None, FLinearColor(0.01f, 0.01f, 0.01f, 0.75f));
		FSlateDrawElement::MakeBox(Elements, TopLayer + 1, Geometry.ToPaintGeometry(KeySize, FSlateLayoutTransform(KeyPosition)),
			White, ESlateDrawEffect::None, FLinearColor(0.9f, 0.9f, 0.9f));
		FSlateDrawElement::MakeText(Elements, TopLayer + 2, Geometry.ToPaintGeometry(Key.Size, FSlateLayoutTransform(KeyPosition + (KeySize - Key.Size) / 2.f)),
			Key.Text, Key.Font, ESlateDrawEffect::None, FLinearColor::Black);
		DrawShadowedText(Elements, TopLayer + 2, Geometry, Prompt,
			FVector2D(KeyPosition.X + KeySize.X + 7, PillPosition.Y + (PillSize.Y - Prompt.Size.Y) / 2.f), FLinearColor::White);
	}

	const double Now = FPlatformTime::Seconds();
	for (const FDamagePopup& Popup : DamagePopups)
	{
		const float Age = Now - Popup.Start;
		FVector2D Position;
		if (Age > 1.2f || !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Popup.Location + FVector(0, 0, Age * 45), Position, true)) continue;
		const FString Text = (Popup.Flags & 2) ? TEXT("MISS") : FString::FromInt(Popup.Damage);
		FSlateDrawElement::MakeText(Elements, TopLayer + 4, Geometry.ToPaintGeometry(FVector2D(100, 30), FSlateLayoutTransform(Position)), Text,
			FCoreStyle::GetDefaultFontStyle("Bold", (Popup.Flags & 1) ? 22 : 18), ESlateDrawEffect::None,
			FLinearColor(1.f, (Popup.Flags & 1) ? 0.7f : 1.f, 0.3f, FMath::Clamp(1.2f - Age, 0.f, 1.f)));
	}
	return TopLayer + 4;
}
