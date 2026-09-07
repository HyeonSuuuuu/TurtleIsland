// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/AgentSkillComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

UAgentSkillComponent::UAgentSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAnimInstance* UAgentSkillComponent::GetAnimInstance() const
{
	if (const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
		{
			return Mesh->GetAnimInstance();
		}
	}
	return nullptr;
}

bool UAgentSkillComponent::IsPlaying() const
{
	const UAnimInstance* Anim = GetAnimInstance();
	return Anim && Anim->IsAnyMontagePlaying();
}

void UAgentSkillComponent::PlaySkill(int32 SkillId, float Yaw)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 서버가 기록한 시전 방향으로 몸을 돌린다. 로컬 플레이어도 예외가 아니다 —
	// 눌렀을 때가 아니라 서버가 승인한 그 방향이 정답이다.
	FRotator Rot = Owner->GetActorRotation();
	Rot.Yaw = Yaw;
	Owner->SetActorRotation(Rot);

	if (UAnimMontage** Found = SkillMontages.Find(SkillId))
	{
		if (UAnimMontage* Montage = *Found)
		{
			if (UAnimInstance* Anim = GetAnimInstance())
			{
				Anim->Montage_Play(Montage, PlayRate);
			}
		}
	}

	OnSkillPlayed.Broadcast(SkillId, Yaw);
}
