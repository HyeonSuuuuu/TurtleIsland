// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHealthChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAgentDamaged, int32, Damage, float, HPRatio, AActor*, Causer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentDied, AActor*, Killer);

/**
 * HP 를 들고 있는 컴포넌트.
 *
 * 중요 — 클라의 HP 는 권위가 아니라 "표시용 캐시"다. 히트 판정과 데미지 계산은 전부 서버가 하고,
 * 여기 있는 Apply* 는 서버가 통보한 결과를 반영만 한다. 클라가 스스로 깎는 함수는 두지 않는다.
 * (PlayerStatsComponent::ApplyServerStats 와 같은 패턴)
 *
 * ACharacter 가 아닌 액터(파괴 가능 오브젝트 등)에도 그대로 붙는다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

public:
	UHealthComponent();

	/** 서버와 맞추기 위해 정수. 실수로 두면 반올림 때문에 표시가 1씩 어긋난다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Health")
	int32 MaxHP = 100;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Health")
	int32 CurrentHP = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Health")
	bool bIsDead = false;

	/** HP 가 바뀔 때마다. HUD 바인딩용 */
	UPROPERTY(BlueprintAssignable, Category="Health")
	FOnHealthChanged OnHealthChanged;

	/** 피격 연출용 */
	UPROPERTY(BlueprintAssignable, Category="Health")
	FOnAgentDamaged OnDamaged;

	/** 사망 연출용 */
	UPROPERTY(BlueprintAssignable, Category="Health")
	FOnAgentDied OnDied;

	/** 서버가 통보한 피격 결과를 반영한다 */
	UFUNCTION(BlueprintCallable, Category="Health")
	void ApplyServerDamage(int32 RemainHP, int32 Damage, AActor* Causer);

	/** 서버가 통보한 회복 결과를 반영한다 */
	UFUNCTION(BlueprintCallable, Category="Health")
	void ApplyServerHeal(int32 RemainHP, int32 Amount);

	/** 서버가 통보한 사망. HP 가 0 이 아니어도 죽일 수 있다 */
	UFUNCTION(BlueprintCallable, Category="Health")
	void ApplyServerDeath(AActor* Killer);

	/** 스폰/리스폰 시 상태 초기화 */
	UFUNCTION(BlueprintCallable, Category="Health")
	void ResetTo(int32 InMaxHP, int32 InHP);

	/** 최대치로 되돌린다. 최대치를 밖에서 꺼내 되돌려줄 필요가 없다 */
	UFUNCTION(BlueprintCallable, Category="Health")
	void ResetToFull();

	/** 최대치는 그대로 두고 현재 HP 만 맞춘다 (서버가 값을 지정할 때) */
	UFUNCTION(BlueprintCallable, Category="Health")
	void ResetHP(int32 InHP);

	UFUNCTION(BlueprintPure, Category="Health")
	float GetHPRatio() const;

	UFUNCTION(BlueprintPure, Category="Health")
	bool IsDead() const { return bIsDead; }
};
