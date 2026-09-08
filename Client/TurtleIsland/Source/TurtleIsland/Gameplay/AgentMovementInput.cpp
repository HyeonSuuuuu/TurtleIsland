// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/AgentMovementInput.h"

UAgentMovementInput::UAgentMovementInput()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UAgentMovementInput::GetViewYaw() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorRotation().Yaw : 0.f;
}
