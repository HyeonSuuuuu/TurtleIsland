// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/AgentMoverComponent.h"
#include "Gameplay/AgentMovementInput.h"
#include "GameFramework/Actor.h"

UAgentMoverComponent::UAgentMoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UAgentMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		Input = Owner->FindComponentByClass<UAgentMovementInput>();
	}
}
