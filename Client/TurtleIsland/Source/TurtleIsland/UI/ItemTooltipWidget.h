#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ItemTooltipWidget.generated.h"

class UImage;
class UNetSubsystem;
class UTextBlock;
class UTexture2D;
class UVerticalBox;

/** 상점·인벤토리 칸 툴팁. 이름·아이콘·능력치(+초록/-빨강)·하단 안내를 인게임 패널 톤으로 그린다. */
UCLASS()
class TURTLEISLAND_API UItemTooltipWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetItem(const UNetSubsystem* Net, int32 ItemId, UTexture2D* ItemIcon, const FString& FooterText);
protected:
	virtual void NativeOnInitialized() override;
private:
	UTextBlock* MakeText(int32 Size, bool bBold, const FLinearColor& Colour);
	void AddStat(const FString& Label, int32 Value);
	UPROPERTY() TObjectPtr<UImage> Icon;
	UPROPERTY() TObjectPtr<UTextBlock> Name;
	UPROPERTY() TObjectPtr<UTextBlock> Kind;
	UPROPERTY() TObjectPtr<UVerticalBox> Stats;
	UPROPERTY() TObjectPtr<UTextBlock> Footer;
};
