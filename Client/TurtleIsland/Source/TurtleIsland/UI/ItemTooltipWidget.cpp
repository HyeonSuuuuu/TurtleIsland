#include "UI/ItemTooltipWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "GameplayRules.h"
#include "Network/NetSubsystem.h"

namespace
{
	// UI 디자인 토큰: Kenney panel-015(48px, 테두리 4px), 큰 패널 틴트, 본문 회색.
	const FLinearColor PanelTint(0.0033f, 0.0144f, 0.0185f, 0.97f);
	const FLinearColor BodyGrey(0.701102f, 0.701102f, 0.701102f, 1.f);
	const FLinearColor FooterGrey(0.701102f, 0.701102f, 0.701102f, 0.65f);
	const FLinearColor EquipmentName(1.f, 0.8f, 0.2f, 1.f);
	const FLinearColor Positive(0.35f, 0.95f, 0.45f, 1.f);
	const FLinearColor Negative(0.95f, 0.38f, 0.38f, 1.f);
}

UTextBlock* UItemTooltipWidget::MakeText(int32 Size, bool bBold, const FLinearColor& Colour)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	const TCHAR* FontPath = bBold ? TEXT("/Game/Fonts/Korean/NotoSansKR-Bold_Font.NotoSansKR-Bold_Font")
		: TEXT("/Game/Fonts/Korean/NotoSansKR-Regular_Font.NotoSansKR-Regular_Font");
	if (UObject* Font = LoadObject<UFont>(nullptr, FontPath)) Text->SetFont(FSlateFontInfo(Font, Size));
	Text->SetColorAndOpacity(Colour);
	return Text;
}

void UItemTooltipWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	if (UTexture2D* PanelTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Textures/Kenney/Panel/panel-015.panel-015")))
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(PanelTexture);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FMargin(0.083333f);
		Brush.ImageSize = FVector2D(48.f, 48.f);
		Panel->SetBrush(Brush);
	}
	Panel->SetBrushColor(PanelTint);
	Panel->SetPadding(FMargin(14.f, 10.f)); // 테두리 4px + 여백
	WidgetTree->RootWidget = Panel;

	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
	Width->SetMinDesiredWidth(220.f);
	Width->SetMaxDesiredWidth(320.f);
	Panel->SetContent(Width);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Width->AddChild(Column);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Header);
	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
	IconBox->SetWidthOverride(40.f); IconBox->SetHeightOverride(40.f);
	Icon = WidgetTree->ConstructWidget<UImage>();
	IconBox->AddChild(Icon);
	Header->AddChildToHorizontalBox(IconBox)->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	UVerticalBox* Titles = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Titles);
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	Name = MakeText(16, true, BodyGrey);
	Kind = MakeText(11, false, FooterGrey);
	Titles->AddChildToVerticalBox(Name);
	Titles->AddChildToVerticalBox(Kind);

	Stats = WidgetTree->ConstructWidget<UVerticalBox>();
	Column->AddChildToVerticalBox(Stats)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	Footer = MakeText(11, false, FooterGrey);
	Footer->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
}

void UItemTooltipWidget::AddStat(const FString& Label, int32 Value)
{
	if (!Value) return;
	UTextBlock* Line = MakeText(13, false, Value > 0 ? Positive : Negative);
	Line->SetText(FText::FromString(FString::Printf(TEXT("%s %s%d"), *Label, Value > 0 ? TEXT("+") : TEXT(""), Value)));
	Stats->AddChildToVerticalBox(Line);
}

void UItemTooltipWidget::SetItem(const UNetSubsystem* Net, int32 ItemId, UTexture2D* ItemIcon, const FString& FooterText)
{
	if (!Name) return;
	const bool bPotion = ItemId == GameplayRules::HealthPotion || ItemId == GameplayRules::ManaPotion;
	const ShopProduct* Product = Net ? Net->FindShopProduct(ItemId) : nullptr;
	Name->SetText(FText::FromString(Net ? Net->GetShopItemName(ItemId) : FString()));
	Name->SetColorAndOpacity(bPotion ? BodyGrey : EquipmentName);
	Kind->SetText(FText::FromString(bPotion ? TEXT("소모품 · 사용하면 사라짐") : TEXT("장비 · 인벤토리에 있으면 적용")));
	Icon->SetBrushFromTexture(ItemIcon, false);
	Icon->SetVisibility(ItemIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	Stats->ClearChildren();
	if (ItemId == GameplayRules::HealthPotion) AddStat(TEXT("HP 회복"), 50);
	else if (ItemId == GameplayRules::ManaPotion) AddStat(TEXT("MP 회복"), 50);
	else if (Product)
	{
		AddStat(TEXT("체력"), Product->hp);
		AddStat(TEXT("공격력"), Product->attack);
		AddStat(TEXT("방어력"), Product->defense);
		AddStat(TEXT("이동속도"), Product->moveSpeed);
	}
	Footer->SetText(FText::FromString(FooterText));
	Footer->SetVisibility(FooterText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
