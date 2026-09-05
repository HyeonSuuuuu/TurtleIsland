// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/OpponentCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

AOpponentCharacter::AOpponentCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
}

void AOpponentCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 원격 프록시: 위치는 직접 보간하므로 자체 이동/중력 끔
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = 0.f;
		Move->bOrientRotationToMovement = false;
		Move->SetMovementMode(MOVE_None);
	}
}

void AOpponentCharacter::UpdateNetState(const FVector& InLocation, const FRotator& InRotation)
{
	TargetLocation = InLocation;
	TargetRotation = FRotator(0.f, InRotation.Yaw, 0.f);

	if (!bHasTarget)
	{
		bHasTarget = true;
		SetActorLocationAndRotation(InLocation, TargetRotation);
	}
}

void AOpponentCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bHasTarget || DeltaTime <= 0.f) return;

	const FVector Cur = GetActorLocation();
	const FVector New = FMath::VInterpTo(Cur, TargetLocation, DeltaTime, InterpSpeed);

	const FVector Vel = (New - Cur) / DeltaTime;
	Speed = Vel.Size2D();
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
		Move->Velocity = Vel;

	SetActorLocation(New);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaTime, InterpSpeed));
}
