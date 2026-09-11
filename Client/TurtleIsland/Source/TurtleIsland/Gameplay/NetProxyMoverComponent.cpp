// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/NetProxyMoverComponent.h"
#include "Gameplay/NetMovementInput.h"
#include "Gameplay/HealthComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UNetProxyMoverComponent::UNetProxyMoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UNetProxyMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	NetInput = Cast<UNetMovementInput>(GetInput());

	// 원격 프록시: 위치는 직접 보간하므로 자체 이동/중력을 끈다
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Move = OwnerChar->GetCharacterMovement())
		{
			Move->GravityScale = 0.f;
			Move->bOrientRotationToMovement = false;
			Move->SetMovementMode(MOVE_None);
			Move->SetComponentTickEnabled(false);
		}
	}
}

void UNetProxyMoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !NetInput || !NetInput->HasTarget() || DeltaTime <= 0.f)
	{
		return;
	}

	const FVector Current = Owner->GetActorLocation();
	if (const UHealthComponent* Health = Owner->FindComponentByClass<UHealthComponent>(); Health && Health->IsDead())
	{
		CurrentSpeed2D = 0.f;
		if (ACharacter* Character = Cast<ACharacter>(Owner)) Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		return;
	}
	const FVector Target  = NetInput->GetTargetLocation();

	FVector NewLocation;
	if (!bPlacedOnce || FVector::Dist2D(Current, Target) > SnapDistance)
	{
		// 첫 배치이거나 오차가 너무 크면 따라가지 않고 즉시 맞춘다
		NewLocation = Target;
	}
	else
	{
		NewLocation = FMath::VInterpTo(Current, Target, DeltaTime, InterpSpeed);
	}

	FVector Velocity = bPlacedOnce ? (NewLocation - Current) / DeltaTime : FVector::ZeroVector;
	if (NetInput->HasPlayerPose()) Velocity.Z = NetInput->GetVerticalVelocity();
	CurrentSpeed2D = Velocity.Size2D();

	if (ACharacter* OwnerChar = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* Move = OwnerChar->GetCharacterMovement())
		{
			Move->Velocity = Velocity;
			if (NetInput->HasPlayerPose())
			{
				// Expose falling state to the AnimBP without running a second physics simulation.
				Move->SetMovementMode(NetInput->IsAirborne() ? MOVE_Falling : MOVE_Walking);
				Move->SetComponentTickEnabled(false);
			}
		}
	}

	Owner->SetActorLocation(NewLocation);

	const FRotator TargetRot(0.f, NetInput->GetFacingYaw(), 0.f);
	Owner->SetActorRotation(bPlacedOnce
		? FMath::RInterpTo(Owner->GetActorRotation(), TargetRot, DeltaTime, InterpSpeed)
		: TargetRot);

	bPlacedOnce = true;
}
