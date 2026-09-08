// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TurtleAgent.generated.h"

class UHealthComponent;
class UAgentSkillComponent;

/**
 * 움직이고 HP 를 갖는 모든 것의 베이스 — 로컬 플레이어, 상대 플레이어, NPC, 몬스터.
 *
 * 무엇이 이 폰을 조종하는지는 여기가 정하지 않는다. 붙어 있는 UAgentMovementInput 구현체가
 * 정한다(ULocalMovementInput = 키보드/마우스, UNetMovementInput = 서버 패킷).
 * 덕분에 싱글/멀티 분기가 "매 프레임 묻는 것"에서 "스폰 때 한 번 조립하는 것"으로 옮겨간다.
 *
 * 구 이름: ATurtleObject (UE 에서 Object 는 UObject 를 뜻해 오해를 부른다)
 */
UCLASS()
class TURTLEISLAND_API ATurtleAgent : public ACharacter
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	/** 사망 시 조작/충돌을 막는다 */
	virtual void EnterDeadState();

	UFUNCTION()
	void HandleDamaged(int32 Damage, float HPRatio, AActor* Causer);

	UFUNCTION()
	void HandleDied(AActor* Killer);

	/** HP 의 소유자. 블루프린트에서는 이 변수로 직접 접근한다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UHealthComponent* Health;

	/** 스킬 연출. 서버 통지를 받아 재생한다 — 로컬이든 원격이든 같은 경로 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UAgentSkillComponent* Skill;

public:
	ATurtleAgent();

	/** 사망 후 조작 불가 상태로 남기는 시간(초). 싱글에서 GameMode 가 이 값으로 리스폰을 예약한다 */
	UPROPERTY(EditAnywhere, Category="Damage")
	float DeathDisableTime = 3.f;

	/** 피격 연출 — 블루프린트에서 구현 */
	UFUNCTION(BlueprintImplementableEvent, Category="Damage")
	void OnDamaged(float Damage, float HPRatio, AActor* Causer);

	/** 사망 연출 — 블루프린트에서 구현 */
	UFUNCTION(BlueprintImplementableEvent, Category="Damage")
	void OnDied();

	/**
	 * 상대 Agent 는 NetSubsystem 의 players, map으로 붙들고 있고 상태만 리셋한다 (재활용)
	 */
	UFUNCTION(BlueprintCallable, Category="Damage")
	virtual void ResetForRespawn(const FVector& At, int32 HP);
};
