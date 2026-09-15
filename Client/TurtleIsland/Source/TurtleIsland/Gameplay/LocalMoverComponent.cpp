// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/LocalMoverComponent.h"
#include "Gameplay/AgentMovementInput.h"
#include "Gameplay/AgentSkillComponent.h"
#include "Gameplay/HealthComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "GameplayRules.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

ULocalMoverComponent::ULocalMoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULocalMoverComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->LandedDelegate.AddDynamic(this, &ULocalMoverComponent::HandleLanded);
		Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
	}

	if (UAgentMovementInput* In = GetInput())
	{
		In->OnJumpPressed.AddUObject(this, &ULocalMoverComponent::HandleJumpPressed);
		In->OnJumpReleased.AddUObject(this, &ULocalMoverComponent::HandleJumpReleased);
	}
}

void ULocalMoverComponent::HandleJumpPressed()
{
	if (IsMovementLocked()) return;
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		OwnerChar->Jump();
	}
}

bool ULocalMoverComponent::IsLandingLocked() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < LandingReadyAt;
}

bool ULocalMoverComponent::IsMovementLocked() const
{
	const UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr;
	const UAgentSkillComponent* Skills = GetOwner() ? GetOwner()->FindComponentByClass<UAgentSkillComponent>() : nullptr;
	return (Health && Health->IsDead()) || IsLandingLocked() || (Skills && Skills->IsMovementLocked());
}

void ULocalMoverComponent::ResetForRespawn()
{
	LandingReadyAt = 0.;
	CurrentSpeed2D = 0.f;
}

void ULocalMoverComponent::HandleLanded(const FHitResult& Hit)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !GetWorld()) return;
	float Duration = GameplayRules::LandingRecoveryMs / 1000.f;
	if (const UAgentSkillComponent* Skills = Character->FindComponentByClass<UAgentSkillComponent>())
	{
		if (Skills->LandAnimation) Duration = FMath::Max(Duration, Skills->LandAnimation->GetPlayLength());
	}
	LandingReadyAt = GetWorld()->GetTimeSeconds() + Duration;
	Character->StopJumping();
	Character->ConsumeMovementInputVector();
	Character->GetCharacterMovement()->StopMovementImmediately();
	CurrentSpeed2D = 0.f;
}

void ULocalMoverComponent::HandleJumpReleased()
{
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		OwnerChar->StopJumping();
	}
}

void ULocalMoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		return;
	}
	if (IsMovementLocked())
	{
		OwnerChar->StopJumping();
		OwnerChar->ConsumeMovementInputVector();
		if (UCharacterMovementComponent* Move = OwnerChar->GetCharacterMovement())
		{
			// Cancel horizontal drift without suspending gravity on moving ground.
			const float VerticalVelocity = Move->Velocity.Z;
			Move->StopMovementImmediately();
			Move->Velocity.Z = VerticalVelocity;
		}
		CurrentSpeed2D = 0.f;
		// 이동은 막혀도 허용된 스킬(E) 시전 중에는 방향키 쪽으로 몸만 돌린다. 판정 방향은 서버가 시전 순간 값으로 고정한다.
		const UAgentSkillComponent* Skills = OwnerChar->FindComponentByClass<UAgentSkillComponent>();
		const UAgentMovementInput* In = GetInput();
		const UHealthComponent* Health = OwnerChar->FindComponentByClass<UHealthComponent>();
		if (Skills && Skills->CanRotateWhileCasting() && In && !IsLandingLocked() && !(Health && Health->IsDead()))
		{
			const FVector2D Dir = In->GetMovementInput();
			if (!Dir.IsNearlyZero())
			{
				const float Rate = OwnerChar->GetCharacterMovement()->RotationRate.Yaw;
				FRotator Rotation = OwnerChar->GetActorRotation();
				Rotation.Yaw = FMath::FixedTurn(Rotation.Yaw, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), Rate * DeltaTime);
				OwnerChar->SetActorRotation(Rotation);
			}
		}
		return;
	}

	if (const UAgentMovementInput* In = GetInput())
	{
		const FVector2D Dir = In->GetMovementInput();
		if (!Dir.IsNearlyZero())
		{
			// 이미 월드 XY 방향이라 컨트롤 회전을 다시 볼 필요가 없다.
			// 회전은 CharacterMovementComponent 의 bOrientRotationToMovement 가 잡는다.
			OwnerChar->AddMovementInput(FVector(Dir.X, Dir.Y, 0.f), 1.f);
		}
	}

	if (const UCharacterMovementComponent* Move = OwnerChar->GetCharacterMovement())
	{
		CurrentSpeed2D = Move->Velocity.Size2D();
	}
}
