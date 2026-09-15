#pragma once

#include "CoreMinimal.h"
#include "UI/ChatLogWidget.h"
#include "Components/Button.h"
#include "MainHUDWidget.generated.h"

class UNetSubsystem;
class UImage;
class UTextBlock;

UCLASS()
class TURTLEISLAND_API UQuickSlotButton : public UButton
{
	GENERATED_BODY()
public:
	UQuickSlotButton();
	void Setup(UNetSubsystem* InNet, int32 InSlot);
private:
	UFUNCTION() void UseSlot();
	UPROPERTY() TObjectPtr<UNetSubsystem> Net;
	int32 SlotIndex = 0;
};

UCLASS()
class TURTLEISLAND_API UMainHUDWidget : public UChatLogWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="HUD") void RefreshState();
	void BindNetwork(UNetSubsystem* InNet);
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	/** 몬스터 이름·체력바를 그리는 최대 거리(cm, 내 캐릭터 기준). 0 이하면 제한 없음. 플레이어 이름표는 항상 그린다. */
	UPROPERTY(EditDefaultsOnly, Category="HUD", meta=(Units="cm"))
	float MonsterPlateMaxDistance = 2500.f;
	/** 진행 중 퀘스트 패널의 화면 위쪽 여백(1080p 기준 px). 미니맵 아래에 오도록 맞춘다. */
	UPROPERTY(EditDefaultsOnly, Category="HUD")
	float QuestPanelTop = 300.f;
private:
	void SetupSlots();
	UFUNCTION() void AddDamage(FVector Location, int32 Damage, uint8 Flags);
	UFUNCTION() void ShowFeedback(FString Message);
	void ScrollCombatLogToEnd();
	UPROPERTY() TObjectPtr<UNetSubsystem> Net;
	UPROPERTY() TArray<TObjectPtr<UQuickSlotButton>> SlotButtons;
	UPROPERTY() TArray<TObjectPtr<UImage>> SlotIcons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotCounts;
	UPROPERTY() TArray<TObjectPtr<class UItemTooltipWidget>> SlotTooltips;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SkillCooldowns;
	struct FDamagePopup { FVector Location; int32 Damage; uint8 Flags; double Start; };
	TArray<FDamagePopup> DamagePopups;
	TArray<FString> CombatLines;
	float LastLayoutWidth = -1.f;
	void SetText(FName Name, const FString& Value);
	void SetBar(FName Name, float Value);
};
