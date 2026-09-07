// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentSkillComponent.generated.h"

class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSkillPlayed, int32, SkillId, float, Yaw);

/**
 * 스킬 "연출"을 재생하는 컴포넌트. 모든 Agent 가 갖는다 — 로컬 플레이어든 상대든 몬스터든.
 *
 * 클라는 스킬을 예측 재생하지 않는다. SC_ATTACK_NOTIFY 를 받아야 재생한다.
 * 덕분에 내 캐릭터와 남의 캐릭터가 완전히 같은 경로를 탄다(예측이 있으면 두 갈래가 된다).
 * 판정·데미지는 전부 서버가 하고, 여기는 보여주기만 한다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UAgentSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentSkillComponent();

	/** 스킬 ID → 몽타주. 블루프린트에서 캐릭터별로 채운다 (0 = 기본공격) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skill")
	TMap<int32, UAnimMontage*> SkillMontages;

	/** 몽타주가 없을 때 쓸 기본 재생 속도 */
	UPROPERTY(EditAnywhere, Category="Skill")
	float PlayRate = 1.f;

	/** 재생 순간 브로드캐스트. 이펙트/사운드는 블루프린트에서 여기 붙인다 */
	UPROPERTY(BlueprintAssignable, Category="Skill")
	FOnSkillPlayed OnSkillPlayed;

	/**
	 * 서버가 시전을 승인했을 때 호출된다. NetSubsystem 의 SC_ATTACK_NOTIFY 핸들러가 부른다.
	 * Yaw 는 시전 순간 서버가 기록한 방향이라, 몽타주 전에 몸을 그쪽으로 돌린다.
	 */
	UFUNCTION(BlueprintCallable, Category="Skill")
	void PlaySkill(int32 SkillId, float Yaw);

	UFUNCTION(BlueprintPure, Category="Skill")
	bool IsPlaying() const;

protected:
	/** 소유 액터의 AnimInstance. 없으면 null */
	class UAnimInstance* GetAnimInstance() const;
};
