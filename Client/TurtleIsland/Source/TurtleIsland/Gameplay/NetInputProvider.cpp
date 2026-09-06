// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/NetInputProvider.h"
#include "Gameplay/GroundDetector.h"
#include "GameFramework/Actor.h"

UNetInputProvider::UNetInputProvider()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNetInputProvider::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		Ground = Owner->FindComponentByClass<UGroundDetector>();
	}
}

float UNetInputProvider::ResolveZ(float WorldX, float WorldY) const
{
	const AActor* Owner = GetOwner();
	const float CurrentZ = Owner ? Owner->GetActorLocation().Z : 0.f;

	if (Ground)
	{
		return Ground->ResolveActorZ(WorldX, WorldY, CurrentZ);
	}
	return CurrentZ;
}

void UNetInputProvider::UpdateNetState(float WorldX, float WorldY, float Yaw)
{
	// BeginPlay 전에 첫 패킷이 올 수 있다
	if (!Ground)
	{
		if (AActor* Owner = GetOwner())
		{
			Ground = Owner->FindComponentByClass<UGroundDetector>();
		}
	}

	TargetLocation = FVector(WorldX, WorldY, ResolveZ(WorldX, WorldY));
	TargetYaw      = Yaw;

	// 첫 패킷은 보간 없이 그 자리에 놓는다
	if (!bHasTarget)
	{
		bHasTarget = true;
		if (AActor* Owner = GetOwner())
		{
			Owner->SetActorLocationAndRotation(TargetLocation, FRotator(0.f, TargetYaw, 0.f));
		}
	}
}

FVector2D UNetInputProvider::GetMovementInput() const
{
	const AActor* Owner = GetOwner();
	if (!bHasTarget || !Owner)
	{
		return FVector2D::ZeroVector;
	}

	const FVector Delta = TargetLocation - Owner->GetActorLocation();
	const FVector2D Flat(Delta.X, Delta.Y);

	const float Dist = Flat.Size();
	if (Dist <= ArriveTolerance)
	{
		return FVector2D::ZeroVector;
	}

	return Flat / Dist;
}
