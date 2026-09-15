// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/AgentSkillComponent.h"
#include "Gameplay/PlayerInputComponent.h"
#include "Gameplay/HealthComponent.h"
#include "Network/NetSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameplayRules.h"

UAgentSkillComponent::UAgentSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
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
	const UHealthComponent* Health = Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;
	const APawn* Pawn = Cast<APawn>(Owner);
	if (!Owner || !Net || IsMovementLocked() || (Health && Health->IsDead()) || !Pawn || !Pawn->IsLocallyControlled() ||
		(Pawn->GetController() && Pawn->GetController()->IsMoveInputIgnored()))
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

void UAgentSkillComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UAnimInstance* Anim = GetAnimInstance();
	if (!Character || !Anim) return;
	const UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
	if (Health && Health->IsDead())
	{
		// Freeze the death clip just before it ends so the body stays down until respawn.
		if (DeathMontage && Anim->Montage_IsPlaying(DeathMontage) &&
			Anim->Montage_GetPosition(DeathMontage) >= DeathMontage->GetPlayLength() - 0.1f)
			Anim->Montage_Pause(DeathMontage);
		if (AirMontage) Anim->Montage_Stop(0.1f, AirMontage);
		AirMontage = nullptr; bWasAirborne = false; return;
	}
	const bool bAirborne = Character->GetCharacterMovement()->IsFalling();
	if (bAirborne && FallAnimation && !bWasAirborne && !Anim->IsAnyMontagePlaying())
	{
		UAnimSequenceBase* Sequence = Character->GetVelocity().Z > 0.f && JumpAnimation ? JumpAnimation.Get() : FallAnimation.Get();
		AirMontage = Anim->PlaySlotAnimationAsDynamicMontage(Sequence, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.f, Sequence == FallAnimation ? 999 : 1);
	}
	else if (bAirborne && FallAnimation && !Anim->IsAnyMontagePlaying())
	{
		AirMontage = Anim->PlaySlotAnimationAsDynamicMontage(FallAnimation, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.f, 999);
	}
	else if (!bAirborne && bWasAirborne)
	{
		const bool bPlayLanding = !Anim->IsAnyMontagePlaying() || (AirMontage && Anim->Montage_IsPlaying(AirMontage));
		if (AirMontage && Anim->Montage_IsPlaying(AirMontage))
		{
			Anim->Montage_Stop(0.1f, AirMontage);
		}
		if (LandAnimation && bPlayLanding)
			Anim->PlaySlotAnimationAsDynamicMontage(LandAnimation, TEXT("DefaultSlot"), 0.1f, 0.1f);
		AirMontage = nullptr;
	}
	bWasAirborne = bAirborne;
}

bool UAgentSkillComponent::IsPlaying() const
{
	const UAnimInstance* Anim = GetAnimInstance();
	return Anim && Anim->IsAnyMontagePlaying();
}

bool UAgentSkillComponent::IsMovementLocked() const
{
	if (GetWorld() && GetWorld()->GetTimeSeconds() < MovementReadyAt) return true;
	const UAnimInstance* Anim = GetAnimInstance();
	return Anim && ActiveSkillMontage && Anim->Montage_IsActive(ActiveSkillMontage);
}

bool UAgentSkillComponent::CanRotateWhileCasting() const
{
	return ActiveSkillId >= 0 && ActiveSkillId < PLAYER_SKILL_COUNT && GameplayRules::Skills[ActiveSkillId].rotateWhileCasting &&
		IsMovementLocked();
}

void UAgentSkillComponent::ResetCast()
{
	MovementReadyAt = 0.;
	ActiveSkillId = INDEX_NONE;
	if (UAnimInstance* Anim = GetAnimInstance())
	{
		if (ActiveSkillMontage) Anim->Montage_Stop(0.f, ActiveSkillMontage);
	}
	ActiveSkillMontage = nullptr;
}

void UAgentSkillComponent::ResetPlayback()
{
	ResetCast();
	if (UAnimInstance* Anim = GetAnimInstance()) Anim->StopAllMontages(0.f);
	AirMontage = nullptr;
	DeathMontage = nullptr;
	bWasAirborne = false;
}

void UAgentSkillComponent::PlayDeath()
{
	UAnimInstance* Anim = GetAnimInstance();
	// 몬스터는 단일 노드 재생으로 자기 사망 클립을 튼다.
	if (!Anim || Anim->IsA<UAnimSingleNodeInstance>()) return;
	UAnimSequenceBase* Sequence = DeathAnimation.LoadSynchronous();
	if (!Sequence) return;
	Anim->StopAllMontages(0.1f);
	// 끝 자세는 TickComponent 가 끝 직전에 멈춰 부활(ResetPlayback)까지 유지한다.
	DeathMontage = Anim->PlaySlotAnimationAsDynamicMontage(Sequence, TEXT("DefaultSlot"), 0.15f, 0.f);
	if (DeathMontage) DeathMontage->bEnableAutoBlendOut = false;
}

void UAgentSkillComponent::PlaySkill(int32 SkillId, float Yaw)
{
	AActor* Owner = GetOwner();
	const UHealthComponent* Health = Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Owner || !GetWorld() || SkillId < 0 || SkillId >= PLAYER_SKILL_COUNT || !FMath::IsFinite(Yaw) ||
		(Health && Health->IsDead()))
	{
		return;
	}

	// 서버가 기록한 시전 방향으로 몸을 돌린다. 로컬 플레이어도 예외가 아니다 —
	// 눌렀을 때가 아니라 서버가 승인한 그 방향이 정답이다.
	FRotator Rotation = Owner->GetActorRotation();
	Rotation.Yaw = Yaw;
	Owner->SetActorRotation(Rotation);
	float LockDuration = GameplayRules::Skills[SkillId].movementLockMs / 1000.f;
	ActiveSkillMontage = nullptr;
	ActiveSkillId = SkillId;

	if (UAnimMontage** Found = SkillMontages.Find(SkillId))
	{
		if (UAnimMontage* Montage = *Found)
		{
			if (UAnimInstance* Anim = GetAnimInstance())
			{
				const float Rate = FMath::IsFinite(PlayRate) && PlayRate > 0.f ? PlayRate : 1.f;
				if (Anim->Montage_Play(Montage, Rate) > 0.f)
				{
					ActiveSkillMontage = Montage;
					const float EffectiveRate = Rate * Montage->RateScale;
					if (EffectiveRate > 0.f && FMath::IsFinite(EffectiveRate))
						LockDuration = FMath::Max(LockDuration, Montage->GetPlayLength() / EffectiveRate);
				}
			}
		}
	}

	// Approval starts recovery even if visual assets are missing or playback is interrupted.
	MovementReadyAt = GetWorld()->GetTimeSeconds() + LockDuration;
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		Character->StopJumping();
		Character->ConsumeMovementInputVector();
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			const float VerticalVelocity = Move->Velocity.Z;
			Move->StopMovementImmediately();
			Move->Velocity.Z = VerticalVelocity;
		}
	}

	OnSkillPlayed.Broadcast(SkillId, Yaw);
}
