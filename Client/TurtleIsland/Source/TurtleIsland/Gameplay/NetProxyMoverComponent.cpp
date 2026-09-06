// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/NetProxyMoverComponent.h"
#include "Gameplay/NetInputProvider.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UNetProxyMoverComponent::UNetProxyMoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UNetProxyMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	NetInput = Cast<UNetInputProvider>(GetInput());

	// 원격 프록시: 위치는 직접 보간하므로 자체 이동/중력을 끈다
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Move = OwnerChar->GetCharacterMovement())
		{
			Move->GravityScale = 0.f;
			Move->bOrientRotationToMovement = false;
			Move->SetMovementMode(MOVE_None);
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
	const FVector Target  = NetInput->GetTargetLocation();

	FVector NewLocation;
	if (FVector::Dist2D(Current, Target) > NetInput->SnapDistance)
	{
		// 오차가 너무 크면 따라가지 않고 즉시 맞춘다
		NewLocation = Target;
	}
	else
	{
		NewLocation = FMath::VInterpTo(Current, Target, DeltaTime, InterpSpeed);
	}

	const FVector Velocity = (NewLocation - Current) / DeltaTime;
	CurrentSpeed2D = Velocity.Size2D();

	if (ACharacter* OwnerChar = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* Move = OwnerChar->GetCharacterMovement())
		{
			Move->Velocity = Velocity;
		}
	}

	Owner->SetActorLocation(NewLocation);

	const FRotator TargetRot(0.f, NetInput->GetFacingYaw(), 0.f);
	Owner->SetActorRotation(FMath::RInterpTo(Owner->GetActorRotation(), TargetRot, DeltaTime, InterpSpeed));
}
