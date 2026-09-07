// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/AgentInputComponent.h"

UAgentInputComponent::UAgentInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UAgentInputComponent::GetViewYaw() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorRotation().Yaw : 0.f;
}
