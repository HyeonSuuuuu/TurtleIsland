#include "UI/QuestBannerWidget.h"

#include "Engine/GameInstance.h"
#include "Network/NetSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/PlayerNameplate.h"

namespace
{
	float DurationOf(EQuestBanner Kind) { return Kind == EQuestBanner::Completed ? 3.2f : 2.4f; }
}

void UQuestBannerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UQuestBannerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNetSubsystem>() : nullptr)
		Net->OnQuestBanner.AddUniqueDynamic(this, &UQuestBannerWidget::Show);
}

void UQuestBannerWidget::NativeDestruct()
{
	if (UNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNetSubsystem>() : nullptr)
		Net->OnQuestBanner.RemoveDynamic(this, &UQuestBannerWidget::Show);
	Queue.Reset();
	Super::NativeDestruct();
}

void UQuestBannerWidget::Show(EQuestBanner Kind, FString Title, FString Detail)
{
	if (Queue.Num() < 6) Queue.Add({Kind, MoveTemp(Title), MoveTemp(Detail)});
}

int32 UQuestBannerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 Top = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled) + 1;
	const double Now = FPlatformTime::Seconds();
	while (Queue.Num() && Queue[0].Start > 0. && Now - Queue[0].Start > DurationOf(Queue[0].Kind)) Queue.RemoveAt(0);
	if (Queue.IsEmpty()) return Top;
	FBanner& Banner = Queue[0];
	if (Banner.Start <= 0.) Banner.Start = Now;

	// Slide in and fade over 0.25s, fade out over the last 0.6s.
	const float Age = static_cast<float>(Now - Banner.Start), Duration = DurationOf(Banner.Kind);
	const float Alpha = FMath::Min(FMath::Clamp(Age / 0.25f, 0.f, 1.f), FMath::Clamp((Duration - Age) / 0.6f, 0.f, 1.f));
	const float Slide = (1.f - FMath::Clamp(Age / 0.25f, 0.f, 1.f)) * -12.f;

	const bool bDone = Banner.Kind == EQuestBanner::Completed;
	const FLinearColor Accent = bDone ? FLinearColor(1.f, 0.8f, 0.3f) : Banner.Kind == EQuestBanner::Ready ? FLinearColor(0.95f, 0.72f, 0.35f) : FLinearColor(0.3f, 0.85f, 0.8f);
	const TCHAR* Heading = bDone ? TEXT("퀘스트 완료") : Banner.Kind == EQuestBanner::Ready ? TEXT("목표 달성") : TEXT("새 퀘스트");
	const auto HeadingText = PlayerNameplate::FitText(Heading, 400, 13, 12);
	const auto TitleText = PlayerNameplate::FitText(Banner.Title, 520, bDone ? 24 : 20, 14);
	const auto DetailText = PlayerNameplate::FitText(Banner.Detail, 520, 13, 11);

	const FVector2D Screen = Geometry.GetLocalSize();
	const float Width = FMath::Max3(HeadingText.Size.X, TitleText.Size.X, DetailText.Size.X) + 96.f;
	const float Height = 22.f + HeadingText.Size.Y + TitleText.Size.Y + (Banner.Detail.IsEmpty() ? 0.f : DetailText.Size.Y + 4.f);
	const FVector2D Panel(Screen.X * 0.5f - Width * 0.5f, Screen.Y * 0.13f + Slide);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const auto Fade = [Alpha](FLinearColor Color) { Color.A *= Alpha; return Color; };
	const auto Box = [&](const FVector2D& At, const FVector2D& Size, const FLinearColor& Color, int32 BoxLayer)
	{
		FSlateDrawElement::MakeBox(Elements, BoxLayer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(At)), White, ESlateDrawEffect::None, Fade(Color));
	};
	const auto Text = [&](const PlayerNameplate::FTextLayout& Layout, float Y, const FLinearColor& Color)
	{
		const FVector2D At(Screen.X * 0.5f - Layout.Size.X * 0.5f, Y);
		FSlateDrawElement::MakeText(Elements, Top + 2, Geometry.ToPaintGeometry(Layout.Size, FSlateLayoutTransform(At + FVector2D(1, 2))),
			Layout.Text, Layout.Font, ESlateDrawEffect::None, Fade(FLinearColor(0, 0, 0, 0.8f)));
		FSlateDrawElement::MakeText(Elements, Top + 3, Geometry.ToPaintGeometry(Layout.Size, FSlateLayoutTransform(At)),
			Layout.Text, Layout.Font, ESlateDrawEffect::None, Fade(Color));
	};

	// Dark ribbon with accent edges, like the HUD panels (docs/ui-hud-spec.md).
	Box(Panel, FVector2D(Width, Height), FLinearColor(0.0033f, 0.0144f, 0.0185f, 0.88f), Top);
	Box(Panel, FVector2D(Width, 2), Accent, Top + 1);
	Box(Panel + FVector2D(0, Height - 2), FVector2D(Width, 2), Accent, Top + 1);
	float Y = Panel.Y + 8.f;
	const float LineWidth = 34.f, Gap = 10.f;
	Box(FVector2D(Screen.X * 0.5f - HeadingText.Size.X * 0.5f - Gap - LineWidth, Y + HeadingText.Size.Y * 0.5f), FVector2D(LineWidth, 1), Accent, Top + 1);
	Box(FVector2D(Screen.X * 0.5f + HeadingText.Size.X * 0.5f + Gap, Y + HeadingText.Size.Y * 0.5f), FVector2D(LineWidth, 1), Accent, Top + 1);
	Text(HeadingText, Y, Accent);
	Y += HeadingText.Size.Y + 2.f;
	Text(TitleText, Y, FLinearColor(0.93f, 0.93f, 0.9f));
	if (!Banner.Detail.IsEmpty()) Text(DetailText, Y + TitleText.Size.Y + 4.f, FLinearColor(0.7011f, 0.7011f, 0.7011f));
	return Top + 4;
}
