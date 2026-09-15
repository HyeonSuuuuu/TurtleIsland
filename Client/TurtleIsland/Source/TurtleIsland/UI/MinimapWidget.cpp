#include "UI/MinimapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/ServerMonster.h"
#include "Character/TurtleAgent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/NpcNameplateComponent.h"
#include "Network/ClientPacketCodec.h"
#include "Network/NetSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/PlayerNameplate.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr float PanelWidth = 240.f, TitleHeight = 26.f, FooterHeight = 30.f, Margin = 24.f;
	const float ZoomMeters[] = {40.f, 80.f, 160.f, 320.f}; // width of the minimap view
	// Linear colors from docs/ui-hud-spec.md
	const FLinearColor PanelColor(0.0033f, 0.0144f, 0.0185f, 0.92f), BorderColor(0.1413f, 0.1f, 0.1095f),
		AccentColor(0.1022f, 0.3968f, 0.3613f), TextColor(0.7011f, 0.7011f, 0.7011f), Gold(1.f, 0.8f, 0.3f),
		MonsterColor(0.85f, 0.16f, 0.13f), PlayerColor(0.3f, 0.85f, 0.8f);

	void Box(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2D& At, const FVector2D& Size, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(At)),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
	}
	void Label(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FString& Text, int32 Size,
		const FVector2D& At, const FLinearColor& Color, float MaxWidth = 400.f)
	{
		const auto Layout = PlayerNameplate::FitText(Text, MaxWidth, Size, FMath::Max(8, Size - 3));
		FSlateDrawElement::MakeText(Elements, Layer, Geometry.ToPaintGeometry(Layout.Size, FSlateLayoutTransform(At + FVector2D(1, 1))),
			Layout.Text, Layout.Font, ESlateDrawEffect::None, FLinearColor::Black);
		FSlateDrawElement::MakeText(Elements, Layer + 1, Geometry.ToPaintGeometry(Layout.Size, FSlateLayoutTransform(At)),
			Layout.Text, Layout.Font, ESlateDrawEffect::None, Color);
	}
	void Frame(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2D& At, const FVector2D& Size)
	{
		Box(Elements, Layer, Geometry, At, Size, BorderColor);
		Box(Elements, Layer + 1, Geometry, At + FVector2D(2, 2), Size - FVector2D(4, 4), PanelColor);
	}
}

void UMinimapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	MapTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Textures/Minimap/T_Minimap.T_Minimap"));
	if (MapTexture)
	{
		MapBrush.SetResourceObject(MapTexture);
		MapBrush.ImageSize = FVector2D(MapTexture->GetSizeX(), MapTexture->GetSizeY());
		MapBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	// Only the minimap rectangle takes the mouse; everything else passes through to the game.
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;
	HitArea = WidgetTree->ConstructWidget<UBorder>();
	HitArea->SetBrushColor(FLinearColor::Transparent);
	UCanvasPanelSlot* HitSlot = Root->AddChildToCanvas(HitArea);
	HitSlot->SetAnchors(FAnchors(1, 0));
	HitSlot->SetAlignment(FVector2D(1, 0));
	HitSlot->SetPosition(FVector2D(-Margin, Margin));
	HitSlot->SetSize(FVector2D(PanelWidth, TitleHeight + (PanelWidth - 16) + FooterHeight));
}

UMinimapWidget::FLayout UMinimapWidget::MakeLayout(const FVector2D& Screen) const
{
	FLayout Layout;
	Layout.MapSize = PanelWidth - 16.f;
	Layout.PanelSize = FVector2D(PanelWidth, TitleHeight + Layout.MapSize + FooterHeight);
	Layout.Panel = FVector2D(Screen.X - Margin - PanelWidth, Margin);
	Layout.MapCenter = Layout.Panel + FVector2D(PanelWidth * 0.5f, TitleHeight + Layout.MapSize * 0.5f);
	Layout.Plus = Layout.Panel + FVector2D(8, TitleHeight + Layout.MapSize + 5);
	Layout.Minus = Layout.Plus + FVector2D(24, 0);
	return Layout;
}

UMinimapWidget* UMinimapWidget::FindFor(const APlayerController* Owner)
{
	for (TObjectIterator<UMinimapWidget> Map; Map; ++Map)
		if (Map->IsInViewport() && Map->GetOwningPlayer() == Owner) return *Map;
	return nullptr;
}

void UMinimapWidget::ToggleFullMap()
{
	bFullMap = !bFullMap;
	if (HitArea) HitArea->SetVisibility(bFullMap ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UMinimapWidget::Zoom(int32 Direction)
{
	ZoomIndex = FMath::Clamp(ZoomIndex - Direction, 0, static_cast<int32>(UE_ARRAY_COUNT(ZoomMeters)) - 1);
}

FReply UMinimapWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const FLayout Layout = MakeLayout(Geometry.GetLocalSize());
	const auto Inside = [&](const FVector2D& At) { return Local.X >= At.X && Local.Y >= At.Y && Local.X <= At.X + 20 && Local.Y <= At.Y + 20; };
	if (!bFullMap && Inside(Layout.Plus)) { Zoom(1); return FReply::Handled(); }
	if (!bFullMap && Inside(Layout.Minus)) { Zoom(-1); return FReply::Handled(); }
	return Super::NativeOnMouseButtonDown(Geometry, Event);
}

FReply UMinimapWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	Zoom(Event.GetWheelDelta() > 0 ? 1 : -1);
	return FReply::Handled();
}

void UMinimapWidget::DrawMap(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2D& Center,
	float ViewSize, float PixelsPerCm, bool bClampChief) const
{
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Me = PC ? PC->GetPawn() : nullptr;
	const UNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNetSubsystem>() : nullptr;
	if (!Me || !MapTexture) return;

	// Screen up is the camera's forward direction.
	const float CameraYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : PC->GetControlRotation().Yaw;
	const FVector2D Forward(FMath::Cos(FMath::DegreesToRadians(CameraYaw)), FMath::Sin(FMath::DegreesToRadians(CameraYaw)));
	const FVector2D Right(-Forward.Y, Forward.X);
	const FVector Focus = bClampChief ? Me->GetActorLocation() : FVector(MapOrigin.X + MapWorldSize * 0.5f, MapOrigin.Y + MapWorldSize * 0.5f, 0);
	const auto ToView = [&](const FVector& World)
	{
		const FVector2D Delta(World.X - Focus.X, World.Y - Focus.Y);
		return Center + FVector2D(FVector2D::DotProduct(Delta, Right), -FVector2D::DotProduct(Delta, Forward)) * PixelsPerCm;
	};

	const FGeometry ViewGeometry = Geometry.MakeChild(FVector2D(ViewSize, ViewSize), FSlateLayoutTransform(Center - FVector2D(ViewSize * 0.5f)));
	Elements.PushClip(FSlateClippingZone(ViewGeometry));
	// Texture x = world X, y = world Y; rotating it by -(yaw + 90) puts camera forward at the top.
	const float TextureSize = MapWorldSize * PixelsPerCm;
	const FVector2D FocusPixel((Focus.X - MapOrigin.X) * PixelsPerCm, (Focus.Y - MapOrigin.Y) * PixelsPerCm);
	FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(FVector2D(TextureSize, TextureSize), FSlateLayoutTransform(Center - FocusPixel),
		FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(-(CameraYaw + 90.f)))), FocusPixel / TextureSize), &MapBrush);

	// Active quest targets and whether the chief has something to report.
	TSet<FString> Targets;
	bool bReportReady = false;
	if (const SC_QuestState* Quests = Net ? Net->GetQuestState() : nullptr)
		for (int32 i = 0; i < Quests->count; ++i)
		{
			const QuestProgress& Quest = Quests->quests[i];
			if (Quest.goal > 0 && Quest.progress >= Quest.goal) bReportReady = true;
			else Targets.Add(FClientPacketCodec::DecodeFixedUtf8(Quest.objective, QUEST_OBJECTIVE_BYTES));
		}

	const float Dot = bClampChief ? 5.f : 6.f;
	for (TActorIterator<ATurtleAgent> It(GetWorld()); It; ++It)
	{
		if (*It == Me || It->IsHidden()) continue;
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsDead()) continue;
		const FVector2D At = ToView(It->GetActorLocation());
		if (const AServerMonster* Monster = Cast<AServerMonster>(*It))
		{
			const bool bTarget = Targets.Contains(Monster->MonsterDisplayName.ToString());
			const float Size = bTarget ? Dot + 2 : Dot;
			Box(Elements, Layer + 1, Geometry, At - FVector2D(Size * 0.5f), FVector2D(Size), bTarget ? Gold : MonsterColor);
		}
		else
		{
			Box(Elements, Layer + 1, Geometry, At - FVector2D(Dot * 0.5f + 1), FVector2D(Dot + 2), PlayerColor);
		}
	}

	for (const TWeakObjectPtr<UNpcNameplateComponent>& Weak : UNpcNameplateComponent::GetAll())
	{
		const UNpcNameplateComponent* Plate = Weak.Get();
		if (!Plate || Plate->GetWorld() != GetWorld() || !Plate->GetOwner()) continue;
		FVector2D At = ToView(Plate->GetOwner()->GetActorLocation());
		const FVector2D Offset = At - Center;
		const float Limit = ViewSize * 0.5f - 7.f;
		if (bClampChief && bReportReady && (FMath::Abs(Offset.X) > Limit || FMath::Abs(Offset.Y) > Limit))
			At = Center + Offset * (Limit / FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y))); // keep the report target on the edge
		const float Pulse = bReportReady ? 3.f * FMath::Abs(FMath::Sin(GetWorld()->GetTimeSeconds() * 3.f)) : 0.f;
		const float Size = 9.f + Pulse;
		FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(FVector2D(Size), FSlateLayoutTransform(At - FVector2D(Size * 0.5f)),
			FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(45.f)))), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Gold);
	}

	// Me: an arrow along the character's facing.
	const float Facing = FMath::DegreesToRadians(Me->GetActorRotation().Yaw);
	const FVector2D MeAt = ToView(Me->GetActorLocation()); // the view center on the minimap, anywhere on the full map
	const FVector2D Direction = (ToView(Me->GetActorLocation() + FVector(FMath::Cos(Facing), FMath::Sin(Facing), 0) * 100.f) - MeAt).GetSafeNormal();
	const FVector2D Side(-Direction.Y, Direction.X);
	const FVector2D Tip = MeAt + Direction * 8.f, Back = MeAt - Direction * 3.f;
	TArray<FVector2D> Arrow = {Tip, MeAt - Direction * 6.f + Side * 6.f, Back, MeAt - Direction * 6.f - Side * 6.f, Tip};
	FSlateDrawElement::MakeLines(Elements, Layer + 3, Geometry.ToPaintGeometry(), Arrow, ESlateDrawEffect::None, FLinearColor::White, true, 2.5f);
	Elements.PopClip();
}

int32 UMinimapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 Top = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled) + 1;
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (!Me || !MapTexture) return Top;
	const FVector2D Screen = Geometry.GetLocalSize();

	if (!bFullMap)
	{
		const FLayout Layout = MakeLayout(Screen);
		Frame(Elements, Top, Geometry, Layout.Panel, Layout.PanelSize);
		Label(Elements, Top + 2, Geometry, TEXT("거북섬"), 12, Layout.Panel + FVector2D(10, 6), AccentColor);
		DrawMap(Elements, Top + 3, Geometry, Layout.MapCenter, Layout.MapSize, Layout.MapSize / (ZoomMeters[ZoomIndex] * 100.f), true);
		for (const auto& [At, Text] : {TPair<FVector2D, const TCHAR*>(Layout.Plus, TEXT("+")), TPair<FVector2D, const TCHAR*>(Layout.Minus, TEXT("-"))})
		{
			Box(Elements, Top + 2, Geometry, At, FVector2D(20, 20), BorderColor);
			Label(Elements, Top + 3, Geometry, Text, 12, At + FVector2D(Text[0] == TEXT('+') ? 6 : 7, 2), TextColor);
		}
		const FVector Location = Me->GetActorLocation();
		const auto Coords = PlayerNameplate::FitText(FString::Printf(TEXT("%d, %d"), FMath::RoundToInt(Location.X / 100), FMath::RoundToInt(Location.Y / 100)), 120, 10, 9);
		Label(Elements, Top + 2, Geometry, Coords.Text, 10,
			Layout.Panel + FVector2D(PanelWidth - 10 - Coords.Size.X, TitleHeight + Layout.MapSize + 8), TextColor);
		return Top + 8;
	}

	// Full map: the whole island centered on screen, same orientation as the minimap.
	Box(Elements, Top, Geometry, FVector2D::ZeroVector, Screen, FLinearColor(0, 0, 0, 0.55f));
	const float MapSize = FMath::Min(Screen.X, Screen.Y) * 0.78f;
	const FVector2D Center = Screen * 0.5f;
	const FVector2D FramePosition = Center - FVector2D(MapSize * 0.5f + 12, MapSize * 0.5f + 40);
	Frame(Elements, Top + 1, Geometry, FramePosition, FVector2D(MapSize + 24, MapSize + 80));
	Label(Elements, Top + 3, Geometry, TEXT("거북섬 전체 지도"), 16, FramePosition + FVector2D(14, 9), AccentColor);
	Label(Elements, Top + 3, Geometry, TEXT("M 닫기"), 12, FramePosition + FVector2D(MapSize - 36, 12), TextColor);
	// A rotated square needs room for its diagonal unless the camera is axis-aligned.
	const float Yaw = FMath::DegreesToRadians(PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : 0.f);
	const float Fit = FMath::Abs(FMath::Cos(Yaw)) + FMath::Abs(FMath::Sin(Yaw));
	DrawMap(Elements, Top + 4, Geometry, Center, MapSize, MapSize / (MapWorldSize * Fit), false);
	FVector2D Legend = FramePosition + FVector2D(14, MapSize + 52);
	for (const auto& [Color, Text] : {TPair<FLinearColor, const TCHAR*>(FLinearColor::White, TEXT("나")), TPair<FLinearColor, const TCHAR*>(PlayerColor, TEXT("플레이어")),
		TPair<FLinearColor, const TCHAR*>(MonsterColor, TEXT("몬스터")), TPair<FLinearColor, const TCHAR*>(Gold, TEXT("퀘스트 대상 · 촌장"))})
	{
		Box(Elements, Top + 3, Geometry, Legend + FVector2D(0, 5), FVector2D(8, 8), Color);
		const auto Layout = PlayerNameplate::FitText(Text, 200, 11, 10);
		Label(Elements, Top + 3, Geometry, Layout.Text, 11, Legend + FVector2D(13, 0), TextColor);
		Legend.X += Layout.Size.X + 34;
	}
	return Top + 12;
}
