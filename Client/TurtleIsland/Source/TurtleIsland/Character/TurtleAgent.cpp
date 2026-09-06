// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/TurtleAgent.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/AgentInputComponent.h"
#include "Gameplay/NetInputProvider.h"
#include "Core/TurtleGameMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"

ATurtleAgent::ATurtleAgent()
{
	PrimaryActorTick.bCanEverTick = true;

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
}

void ATurtleAgent::BeginPlay()
{
	Super::BeginPlay();

	if (Health)
	{
		// 블루프린트에 저작해둔 MaxHP 를 컴포넌트로 옮긴다
		const int32 Authored = FMath::Max(1, FMath::RoundToInt(MaxHP));
		Health->ResetTo(Authored, Authored);

		Health->OnDamaged.AddDynamic(this, &ATurtleAgent::HandleDamaged);
		Health->OnDied.AddDynamic(this, &ATurtleAgent::HandleDied);
		Health->OnHealthChanged.AddDynamic(this, &ATurtleAgent::HandleHealthChanged);

		HandleHealthChanged();
	}
}

float ATurtleAgent::GetHPRatio() const
{
	return Health ? Health->GetHPRatio() : 0.f;
}

void ATurtleAgent::HandleHealthChanged()
{
	if (Health)
	{
		CurrentHP = static_cast<float>(Health->CurrentHP);
		bIsDead   = Health->bIsDead;
	}
}

void ATurtleAgent::HandleDamaged(int32 Damage, float HPRatio, AActor* Causer)
{
	HandleHealthChanged();
	OnDamaged(static_cast<float>(Damage), HPRatio, Causer);
}

void ATurtleAgent::HandleDied(AActor* Killer)
{
	HandleHealthChanged();
	EnterDeadState();
	OnDied();

	// 리스폰 "실행"은 GameMode 한 곳에만 있다. 싱글이면 타이머, 멀티면 서버 통지를 기다린다.
	if (ATurtleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ATurtleGameMode>() : nullptr)
	{
		GM->ScheduleRespawn(this, DeathDisableTime);
	}
}

void ATurtleAgent::EnterDeadState()
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (AController* C = GetController())
	{
		C->SetIgnoreMoveInput(true);
		C->SetIgnoreLookInput(true);
	}
}

void ATurtleAgent::ResetForRespawn(const FVector& At, int32 HP)
{
	if (Health)
	{
		Health->ResetTo(Health->MaxHP, HP > 0 ? HP : Health->MaxHP);
	}

	if (!At.IsNearlyZero())
	{
		SetActorLocation(At);
	}

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetMovementMode(MOVE_Walking);
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (AController* C = GetController())
	{
		C->SetIgnoreMoveInput(false);
		C->SetIgnoreLookInput(false);
	}

	HandleHealthChanged();
}

void ATurtleAgent::UpdateNetState(float WorldX, float WorldY, float Yaw)
{
	if (UNetInputProvider* NetInput = FindComponentByClass<UNetInputProvider>())
	{
		NetInput->UpdateNetState(WorldX, WorldY, Yaw);
	}
}
