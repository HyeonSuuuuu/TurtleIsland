// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/TurtleAgent.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/AgentSkillComponent.h"
#include "Gameplay/LocalMoverComponent.h"
#include "Gameplay/PlayerInputComponent.h"
#include "Core/TurtleGameMode.h"
#include "TurtleIsland.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"

ATurtleAgent::ATurtleAgent()
{
	PrimaryActorTick.bCanEverTick = true;

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
	Skill  = CreateDefaultSubobject<UAgentSkillComponent>(TEXT("Skill"));
}

void ATurtleAgent::BeginPlay()
{
	Super::BeginPlay();

	// HP 초기화는 UHealthComponent::BeginPlay 가 자기 MaxHP 로 직접 한다.
	// 여기서는 연출을 붙이기 위해 델리게이트만 구독한다.
	if (Health)
	{
		Health->OnDamaged.AddDynamic(this, &ATurtleAgent::HandleDamaged);
		Health->OnDied.AddDynamic(this, &ATurtleAgent::HandleDied);
	}
}

void ATurtleAgent::HandleDamaged(int32 Damage, float HPRatio, AActor* Causer)
{
	OnDamaged(static_cast<float>(Damage), HPRatio, Causer);
}

void ATurtleAgent::HandleDied(AActor* Killer)
{
	EnterDeadState();
	OnDied();

	// Single play uses the GameMode timer; multiplayer waits for SC_RESPAWN_NOTIFY.
	ATurtleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ATurtleGameMode>() : nullptr;
	if (!GM)
	{
		// 조용히 넘어가면 "죽었는데 영영 안 살아나는" 증상으로만 드러난다.
		UE_LOG(LogTurtleIsland, Error,
			TEXT("%s: 현재 GameMode 가 ATurtleGameMode 파생이 아니라 리스폰을 예약할 수 없다. ")
			TEXT("레벨의 GameMode 블루프린트를 TurtleGameMode 파생으로 바꿔야 한다."),
			*GetNameSafe(this));
		return;
	}
	GM->ScheduleRespawn(this, DeathDisableTime);
}

void ATurtleAgent::EnterDeadState()
{
	if (Skill)
	{
		Skill->ResetPlayback();
		Skill->PlayDeath();
	}
	StopJumping();
	ConsumeMovementInputVector();
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
		if (!DeathController.IsValid())
		{
			DeathController = C;
			C->SetIgnoreMoveInput(true);
			C->SetIgnoreLookInput(true);
		}
	}
}

void ATurtleAgent::ResetForRespawn(const FVector& At, int32 HP)
{
	if (Skill) Skill->ResetPlayback();
	StopJumping();
	ConsumeMovementInputVector();
	if (ULocalMoverComponent* Mover = FindComponentByClass<ULocalMoverComponent>()) Mover->ResetForRespawn();
	if (UPlayerInputComponent* Input = FindComponentByClass<UPlayerInputComponent>())
	{
		Input->OnMoveReleased.Broadcast();
		Input->OnJumpReleased.Broadcast();
	}
	if (Health)
	{
		// 서버가 값을 주면 그 값으로, 아니면 컴포넌트가 알아서 최대치로
		if (HP > 0) { Health->ResetHP(HP); }
		else        { Health->ResetToFull(); }
	}

	SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetMovementMode(MOVE_Walking);
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (AController* C = DeathController.Get())
	{
		C->SetIgnoreMoveInput(false);
		C->SetIgnoreLookInput(false);
	}
	DeathController.Reset();

}
