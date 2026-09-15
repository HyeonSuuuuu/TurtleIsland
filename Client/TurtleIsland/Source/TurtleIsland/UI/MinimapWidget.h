#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "MinimapWidget.generated.h"

class UTexture2D;

/**
 *  우측 상단 미니맵과 M 키 전체 지도. 지도 그림은 Content/Python/create_minimap.py 가 만든 T_Minimap 이고,
 *  위에 나·다른 플레이어·몬스터(퀘스트 대상은 금색)·촌장을 그린다. 화면 위쪽이 카메라가 보는 방향이다.
 *  확대·축소는 +/- 버튼(커서가 보일 때), 마우스 휠, 키보드 +/- 로 한다.
 */
UCLASS(Config=Game)
class TURTLEISLAND_API UMinimapWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** T_Minimap 이 덮는 월드 사각형(cm). create_minimap.py 가 DefaultGame.ini 에 쓴다. */
	UPROPERTY(Config) FVector2D MapOrigin = FVector2D(-7400., -16700.);
	UPROPERTY(Config) float MapWorldSize = 53300.f;

	void ToggleFullMap();
	bool IsFullMap() const { return bFullMap; }
	void Zoom(int32 Direction); // +1 = closer
	static UMinimapWidget* FindFor(const APlayerController* Owner);

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	struct FLayout { FVector2D Panel, PanelSize, MapCenter, Plus, Minus; float MapSize; };
	FLayout MakeLayout(const FVector2D& Screen) const;
	void DrawMap(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2D& Center,
		float ViewSize, float PixelsPerCm, bool bClampChief) const;

	UPROPERTY(Transient) TObjectPtr<UTexture2D> MapTexture;
	UPROPERTY(Transient) TObjectPtr<class UBorder> HitArea;
	FSlateBrush MapBrush;
	int32 ZoomIndex = 1;
	bool bFullMap = false;
};
