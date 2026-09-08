// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/AgentSkillComponent.h"
#include "Gameplay/PlayerInputComponent.h"
#include "Network/NetSubsystem.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UAgentSkillComponent::UAgentSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgentSkillComponent::BeginPlay()
{
	Super::BeginPlay();

	// 로컬 플레이어에만 입력 컴포넌트가 있다. 원격 Agent 에서는 이 구독이 그냥 일어나지 않는다.
	if (AActor* Owner = GetOwner())
	{
		if (UPlayerInputComponent* PlayerInput = Owner->FindComponentByClass<UPlayerInputComponent>())
		{
			PlayerInput->OnSkillPressed.AddUObject(this, &UAgentSkillComponent::RequestSkill);
		}
	}
}

void UAgentSkillComponent::RequestSkill(int32 SkillId)
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UNetSubsystem* Net = GI ? GI->GetSubsystem<UNetSubsystem>() : nullptr;
	if (!Owner || !Net)
	{
		return;
	}

	// 의도만 보낸다. 재생은 서버가 SC_ATTACK_NOTIFY 로 돌려줄 때.
	Net->SendAttack(SkillId, CurrentTargetId, Owner->GetActorRotation().Yaw);
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
