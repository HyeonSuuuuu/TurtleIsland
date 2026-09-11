// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerStatsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStatsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32, NewLevel);

/**
 * 레벨/경험치/골드를 들고 있는 플레이어 스탯 컴포넌트.
 * 현재는 클라가 값을 직접 굴리지만, 서버 권위로 옮길 때는 AddExp/AddGold 대신
 * ApplyServerStats 만 호출하도록 바꾸면 UI 쪽은 그대로 둘 수 있다.
 * (HP 는 UHealthComponent 가 따로 소유한다)
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UPlayerStatsComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	/** 현재 Level 기준 ExpToNext 재계산 */
	void RecalcExpToNext();

public:
	UPlayerStatsComponent();

	/** 레벨/경험치/골드 중 하나라도 바뀌면 브로드캐스트 (HUD 갱신용) */
	UPROPERTY(BlueprintAssignable, Category="Stats")
	FOnStatsChanged OnStatsChanged;

	/** 레벨업 순간에만 브로드캐스트 (연출용) */
	UPROPERTY(BlueprintAssignable, Category="Stats")
	FOnLevelUp OnLevelUp;

	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 Level = 1;

	/** 현재 레벨 구간에서 쌓은 경험치 */
	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 Exp = 0;

	/** 다음 레벨까지 필요한 경험치 */
	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 ExpToNext = 100;

	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 Gold = 0;
	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 MP = 100;
	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 MaxMP = 100;
	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 Attack = 20;
	UPROPERTY(BlueprintReadOnly, Category="Stats")
	int32 Defense = 3;
	bool bServerDriven = false;

	/** 1레벨 → 2레벨 필요 경험치 */
	UPROPERTY(EditAnywhere, Category="Stats|Curve")
	int32 BaseExpToNext = 100;

	/** 레벨당 필요 경험치 증가량 (등차) */
	UPROPERTY(EditAnywhere, Category="Stats|Curve")
	int32 ExpGrowthPerLevel = 50;

	UPROPERTY(EditAnywhere, Category="Stats|Curve")
	int32 MaxLevel = 30;

	/** 시작 골드 */
	UPROPERTY(EditAnywhere, Category="Stats")
	int32 StartingGold = 0;

	/** 경험치 획득. 한 번에 여러 레벨이 올라갈 수 있다. */
	UFUNCTION(BlueprintCallable, Category="Stats")
	void AddExp(int32 Amount);

	UFUNCTION(BlueprintCallable, Category="Stats")
	void AddGold(int32 Amount);

	/** 상점 결제용. 잔액이 모자라면 아무것도 하지 않고 false */
	UFUNCTION(BlueprintCallable, Category="Stats")
	bool SpendGold(int32 Amount);

	/** 경험치 바 채움 비율 0~1 */
	UFUNCTION(BlueprintPure, Category="Stats")
	float GetExpRatio() const;

	UFUNCTION(BlueprintPure, Category="Stats")
	bool IsMaxLevel() const { return Level >= MaxLevel; }

	/** 서버 권위 전환용 진입점. 서버가 보낸 값으로 통째로 덮어쓴다. */
	void ApplyServerStats(int32 InLevel, int32 InExp, int32 InExpToNext, int32 InGold);
};
