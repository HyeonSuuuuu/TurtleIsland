#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/NetSubsystem.h"
#include "QuestBannerWidget.generated.h"

/**
 *  화면 위쪽 가운데에 잠깐 뜨는 퀘스트 배너(새 퀘스트 / 목표 달성 / 퀘스트 완료).
 *  상점 창 위에도 보이도록 높은 ZOrder 로 뷰포트에 따로 붙고, 입력은 받지 않는다. 여러 개면 차례로 띄운다.
 */
UCLASS()
class TURTLEISLAND_API UQuestBannerWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION() void Show(EQuestBanner Kind, FString Title, FString Detail);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	struct FBanner { EQuestBanner Kind; FString Title, Detail; double Start = 0.; };
	mutable TArray<FBanner> Queue; // paint starts and retires banners
};
