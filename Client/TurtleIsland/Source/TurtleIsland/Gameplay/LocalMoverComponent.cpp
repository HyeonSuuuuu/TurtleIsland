// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/LocalMoverComponent.h"
#include "Gameplay/AgentMovementInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

ULocalMoverComponent::ULocalMoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULocalMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UAgentMovementInput* In = GetInput())
	{
		In->OnJumpPressed.AddUObject(this, &ULocalMoverComponent::HandleJumpPressed);
		In->OnJumpReleased.AddUObject(this, &ULocalMoverComponent::HandleJumpReleased);
	}
}

void ULocalMoverComponent::HandleJumpPressed()
{
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		OwnerChar->Jump();
	}
}

void ULocalMoverComponent::HandleJumpReleased()
{
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		OwnerChar->StopJumping();
	}
}

void ULocalMoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		return;
	}

	if (const UAgentMovementInput* In = GetInput())
	{
		const FVector2D Dir = In->GetMovementInput();
		if (!Dir.IsNearlyZero())
		{
			// 이미 월드 XY 방향이라 컨트롤 회전을 다시 볼 필요가 없다.
			// 회전은 CharacterMovementComponent 의 bOrientRotationToMovement 가 잡는다.
			OwnerChar->AddMovementInput(FVector(Dir.X, Dir.Y, 0.f), 1.f);
		}
	}

	if (const UCharacterMovementComponent* Move = OwnerChar->GetCharacterMovement())
	{
		CurrentSpeed2D = Move->Velocity.Size2D();
	}
}
