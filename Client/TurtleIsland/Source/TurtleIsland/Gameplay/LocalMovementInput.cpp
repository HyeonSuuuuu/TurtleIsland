// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/LocalMovementInput.h"
#include "Gameplay/PlayerInputComponent.h"
#include "Character/PlayerAgent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

ULocalMovementInput::ULocalMovementInput()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULocalMovementInput::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		if (UPlayerInputComponent* PlayerInput = Owner->FindComponentByClass<UPlayerInputComponent>())
		{
			PlayerInput->OnMoveAxis.AddUObject(this, &ULocalMovementInput::HandleMoveAxis);
			PlayerInput->OnMoveReleased.AddUObject(this, &ULocalMovementInput::HandleMoveReleased);
			PlayerInput->OnLookAxis.AddUObject(this, &ULocalMovementInput::HandleLookAxis);
			PlayerInput->OnJumpPressed.AddUObject(this, &ULocalMovementInput::HandleJumpPressed);
			PlayerInput->OnJumpReleased.AddUObject(this, &ULocalMovementInput::HandleJumpReleased);
		}
	}
}

float ULocalMovementInput::GetViewYaw() const
{
	if (const APlayerAgent* Player = Cast<APlayerAgent>(GetOwner())) return Player->GetCameraViewYaw();
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const AController* C = OwnerPawn->GetController())
		{
			return C->GetControlRotation().Yaw;
		}
		return OwnerPawn->GetActorRotation().Yaw;
	}
	return 0.f;
}

FVector2D ULocalMovementInput::ToWorldDirection(const FVector2D& LocalAxis) const
{
	const FRotator YawRotation(0.f, GetViewYaw(), 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// LocalAxis.X = Right, LocalAxis.Y = Forward
	const FVector World = Forward * LocalAxis.Y + Right * LocalAxis.X;
	return FVector2D(World.X, World.Y);
}

void ULocalMovementInput::HandleMoveAxis(const FVector2D& LocalAxis)
{
	CachedWorldDirection = ToWorldDirection(LocalAxis);
}

void ULocalMovementInput::HandleMoveReleased()
{
	CachedWorldDirection = FVector2D::ZeroVector;
}

void ULocalMovementInput::HandleLookAxis(const FVector2D& LookAxis)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (Cast<APlayerAgent>(OwnerPawn)) return;
	if (!OwnerPawn || !OwnerPawn->GetController())
	{
		return;
	}

	OwnerPawn->AddControllerYawInput(LookAxis.X);
	OwnerPawn->AddControllerPitchInput(LookAxis.Y);
}

void ULocalMovementInput::HandleJumpPressed()  { OnJumpPressed.Broadcast(); }
void ULocalMovementInput::HandleJumpReleased() { OnJumpReleased.Broadcast(); }

float ULocalMovementInput::GetFacingYaw() const
{
	// 로컬 폰은 CharacterMovementComponent 의 bOrientRotationToMovement 가 회전을 잡는다.
	// 값 자체는 참고용으로만 돌려준다 (WantsFacingOverride() == false).
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorRotation().Yaw;
	}
	return 0.f;
}
