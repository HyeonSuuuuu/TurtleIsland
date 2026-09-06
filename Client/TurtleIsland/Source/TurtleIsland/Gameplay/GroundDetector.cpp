// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/GroundDetector.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

UGroundDetector::UGroundDetector()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UGroundDetector::GetFootOffset() const
{
	if (const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (const UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent())
		{
			return Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	return 0.f;
}

float UGroundDetector::ResolveActorZ(float WorldX, float WorldY, float SearchAroundZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FallbackZ;
	}

	const FVector Start(WorldX, WorldY, SearchAroundZ + TraceUp);
	const FVector End  (WorldX, WorldY, SearchAroundZ + TraceUp - TraceDown);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GroundDetector), false, GetOwner());
	Params.bTraceComplex = false;

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return Hit.ImpactPoint.Z + GetFootOffset();
	}

	return FallbackZ;
}
