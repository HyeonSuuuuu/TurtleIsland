// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SkillInputComponent.generated.h"

class UInputAction;
class UInputComponent;
struct FInputActionValue;

/**
 * 전투 입력(좌클릭 / Q / E)을 서버로 올린다. 로컬 플레이어에게만 붙는다.
 *
 * 이동 입력(UAgentInputComponent)과 일부러 분리했다. 저쪽 계약은 "월드 XY 이동 방향"이고,
 * 스킬은 방향이 아니라 이벤트라 성격이 다르다.
 *
 * 여기서 몽타주를 재생하지 않는다. 재생은 서버가 SC_ATTACK_NOTIFY 로 승인한 뒤
 * UAgentSkillComponent 가 한다. 이 컴포넌트가 하는 일은 "의도를 보내는 것"뿐이다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API USkillInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkillInputComponent();

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* AttackAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* SkillQAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* SkillEAction = nullptr;

	/** 각 입력이 보낼 스킬 ID. 0 은 기본공격 */
	UPROPERTY(EditAnywhere, Category="Skill")
	int32 AttackSkillId = 0;

	UPROPERTY(EditAnywhere, Category="Skill")
	int32 SkillQId = 1;

	UPROPERTY(EditAnywhere, Category="Skill")
	int32 SkillEId = 2;

	/** 폰의 SetupPlayerInputComponent 에서 불러준다 */
	void BindInput(UInputComponent* PlayerInputComponent);

	/** 타겟 지정형 스킬이 쓸 현재 타겟. 논타겟이면 0 */
	UPROPERTY(BlueprintReadWrite, Category="Skill")
	int64 CurrentTargetId = 0;

protected:
	void HandleAttack(const FInputActionValue& Value);
	void HandleSkillQ(const FInputActionValue& Value);
	void HandleSkillE(const FInputActionValue& Value);

	/** CS_ATTACK 송신. 싱글 모드면 아무것도 하지 않는다 */
	void RequestSkill(int32 SkillId);
};
