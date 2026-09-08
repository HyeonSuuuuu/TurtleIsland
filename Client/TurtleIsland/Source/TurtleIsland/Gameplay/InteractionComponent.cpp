// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/InteractionComponent.h"
#include "Gameplay/PlayerInputComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		if (UPlayerInputComponent* PlayerInput = Owner->FindComponentByClass<UPlayerInputComponent>())
		{
			PlayerInput->OnInteractPressed.AddUObject(this, &UInteractionComponent::HandleInteract);
		}
	}
}

AActor* UInteractionComponent::FindNearestTarget() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	const FVector From = Owner->GetActorLocation();

	// 월드의 모든 액터를 훑지 않는다. 콜리전에게 사거리 안의 후보만 물어본다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractScan), false, Owner);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps, From, FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
		FCollisionShape::MakeSphere(Range), Params);

	AActor* Best = nullptr;
	float BestSq = TNumericLimits<float>::Max();

	for (const FOverlapResult& O : Overlaps)
	{
		AActor* Candidate = O.GetActor();
		if (!IsValid(Candidate) || Candidate == Owner || !Candidate->ActorHasTag(InteractableTag))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(From, Candidate->GetActorLocation());
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ScanAccum += DeltaTime;
	if (ScanAccum < ScanInterval)
	{
		return;
	}
	ScanAccum = 0.f;

	AActor* Found = FindNearestTarget();
	if (Found != CurrentTarget)
	{
		CurrentTarget = Found;
		OnInteractTargetChanged.Broadcast(CurrentTarget);
	}
}

void UInteractionComponent::HandleInteract()
{
	if (CurrentTarget)
	{
		OnInteracted.Broadcast(CurrentTarget);
	}
}
