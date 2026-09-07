// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/InteractionComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractionComponent::BindInput(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (InteractAction)
		{
			EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &UInteractionComponent::HandleInteract);
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
	const float RangeSq = Range * Range;

	AActor* Best = nullptr;
	float BestSq = RangeSq;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!IsValid(Candidate) || Candidate == Owner || !Candidate->ActorHasTag(InteractableTag))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(From, Candidate->GetActorLocation());
		if (DistSq <= BestSq)
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

void UInteractionComponent::HandleInteract(const FInputActionValue& Value)
{
	if (CurrentTarget)
	{
		OnInteracted.Broadcast(CurrentTarget);
	}
}
