// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentSkillComponent.generated.h"

class UAnimMontage;
class UAnimSequenceBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSkillPlayed, int32, SkillId, float, Yaw);

/**
 * 이 Agent 의 스킬. 모든 Agent 가 갖는다 — 로컬 플레이어든 상대든 몬스터든.
 *
 * 두 방향을 다 맡는다.
 *   요청 : 로컬 입력을 받아 CS_ATTACK 을 보낸다. 원격 Agent 에는 UPlayerInputComponent 가
 *          없어 구독이 일어나지 않으므로, 같은 클래스를 붙여도 아무 일도 생기지 않는다.
 *   재생 : SC_ATTACK_NOTIFY 를 받아 몽타주를 재생한다.
 *
 * 예측하지 않는 게 핵심이다. 키를 눌러도 여기서 재생하지 않고 서버 승인을 기다린다.
 * 그래서 내 캐릭터와 남의 캐릭터가 완전히 같은 재생 경로를 탄다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UAgentSkillComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	/** CS_ATTACK 송신. 여기서 몽타주를 재생하지 않는다 */
	void RequestSkill(int32 SkillId);

	/** 소유 액터의 AnimInstance. 없으면 null */
	class UAnimInstance* GetAnimInstance() const;

public:
	UAgentSkillComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	UPROPERTY(EditAnywhere, Category="Skill|Jump") TObjectPtr<UAnimSequenceBase> JumpAnimation;
	UPROPERTY(EditAnywhere, Category="Skill|Jump") TObjectPtr<UAnimSequenceBase> FallAnimation;
	UPROPERTY(EditAnywhere, Category="Skill|Jump") TObjectPtr<UAnimSequenceBase> LandAnimation;
	/** 검 트레일: 칼날 본과 그 본 기준 손잡이·끝 오프셋(Paladin Sword_joint 는 -Z 가 칼날). */
	UPROPERTY(EditAnywhere, Category="Skill|Trail") FName TrailBone = TEXT("Sword_joint");
	UPROPERTY(EditAnywhere, Category="Skill|Trail") FVector TrailBaseOffset = FVector(0, 0, -15);
	UPROPERTY(EditAnywhere, Category="Skill|Trail") FVector TrailTipOffset = FVector(0, 0, -85);
	/** 평타, Q, E 트레일 색(가산이라 1 이상이면 더 밝다). */
	UPROPERTY(EditAnywhere, Category="Skill|Trail") TArray<FLinearColor> TrailColours = {
		FLinearColor(1.6f, 2.6f, 3.2f), FLinearColor(4.f, 2.8f, 1.f), FLinearColor(3.f, 1.4f, 4.f)};
	/** 사망 시 한 번 재생하고 마지막 자세로 부활까지 유지한다. 비워두면 Paladin 기본 사망 모션. */
	UPROPERTY(EditAnywhere, Category="Skill|Death") TSoftObjectPtr<UAnimSequenceBase> DeathAnimation =
		TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/Lv3_Ingame/Characters/Paladin/Anims/Sword_and_Shield_Animation/sword_and_shield_death.sword_and_shield_death")));

	/** 스킬 ID → 몽타주. 블루프린트에서 캐릭터별로 채운다 (0 = 기본공격) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skill")
	TMap<int32, UAnimMontage*> SkillMontages;

	UPROPERTY(EditAnywhere, Category="Skill")
	float PlayRate = 1.f;

	/** 타겟 지정형 스킬이 쓸 현재 타겟. 논타겟이면 0 */
	UPROPERTY(BlueprintReadWrite, Category="Skill")
	int64 CurrentTargetId = -1;

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

	UFUNCTION(BlueprintPure, Category="Skill")
	bool IsMovementLocked() const;
	/** 이동은 잠겨도 방향키로 몸을 돌릴 수 있는 시전 중인지(GameplayRules::SkillDef::rotateWhileCasting). */
	bool CanRotateWhileCasting() const;
	void ResetCast();
	void ResetPlayback();
	void PlayDeath();
private:
	double MovementReadyAt = 0.;
	int32 ActiveSkillId = INDEX_NONE;
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveSkillMontage;
	bool bWasAirborne = false;
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> AirMontage;
};
