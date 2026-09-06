// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TurtleAgent.generated.h"

class UHealthComponent;
class UAgentInputComponent;
class UAgentMoverComponent;

/**
 * 움직이고 HP 를 갖는 모든 것의 베이스 — 로컬 플레이어, 상대 플레이어, NPC, 몬스터.
 *
 * 무엇이 이 폰을 조종하는지는 여기가 정하지 않는다. 붙어 있는 UAgentInputComponent 구현체가
 * 정한다(ULocalInputProvider = 키보드/마우스, UNetInputProvider = 서버 패킷).
 * 덕분에 싱글/멀티 분기가 "매 프레임 묻는 것"에서 "스폰 때 한 번 조립하는 것"으로 옮겨간다.
 *
 * 구 이름: ATurtleObject (UE 에서 Object 는 UObject 를 뜻해 오해를 부른다)
 */
UCLASS()
class TURTLEISLAND_API ATurtleAgent : public ACharacter
{
	GENERATED_BODY()

public:
	ATurtleAgent();

	/**
	 * 블루프린트에 물려둔 값을 살리기 위한 저작용 기본값. BeginPlay 에서 HealthComponent 를 이 값으로 채운다.
	 * 실제 HP 의 소유자는 UHealthComponent 다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage")
	float MaxHP = 100.f;

	/** HealthComponent 를 따라가는 읽기 전용 미러. 기존 블루프린트 그래프 호환용 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Damage")
	float CurrentHP = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Damage")
	bool bIsDead = false;

	/** 사망 후 조작 불가 상태로 남기는 시간(초). 싱글에서 GameMode 가 이 값으로 리스폰을 예약한다 */
	UPROPERTY(EditAnywhere, Category="Damage")
	float DeathDisableTime = 3.f;

	UFUNCTION(BlueprintPure, Category="Damage")
	UHealthComponent* GetHealth() const { return Health; }

	/** 체력 비율 0~1 */
	UFUNCTION(BlueprintPure, Category="Damage")
	float GetHPRatio() const;

	/** 피격 연출 — 블루프린트에서 구현 */
	UFUNCTION(BlueprintImplementableEvent, Category="Damage")
	void OnDamaged(float Damage, float HPRatio, AActor* Causer);

	/** 사망 연출 — 블루프린트에서 구현 */
	UFUNCTION(BlueprintImplementableEvent, Category="Damage")
	void OnDied();

	/** 서버가 보낸 목표 상태를 입력 컴포넌트에 전달한다. 프록시가 아니면 아무것도 하지 않는다 */
	void UpdateNetState(float WorldX, float WorldY, float Yaw);

	/**
	 * 리스폰. 폰을 새로 스폰하지 않고 상태만 되돌린다 —
	 * 상대 Agent 는 NetSubsystem 의 players 맵이 붙들고 있어서 Destroy 하면 맵이 깨진다.
	 */
	UFUNCTION(BlueprintCallable, Category="Damage")
	virtual void ResetForRespawn(const FVector& At, int32 HP);

protected:
	virtual void BeginPlay() override;

	/** 사망 시 조작/충돌을 막는다 */
	virtual void EnterDeadState();

	UFUNCTION()
	void HandleDamaged(int32 Damage, float HPRatio, AActor* Causer);

	UFUNCTION()
	void HandleDied(AActor* Killer);

	UFUNCTION()
	void HandleHealthChanged();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UHealthComponent* Health;
};
