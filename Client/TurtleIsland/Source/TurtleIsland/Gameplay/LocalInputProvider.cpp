// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/LocalInputProvider.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "TurtleIsland.h"

ULocalInputProvider::ULocalInputProvider()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULocalInputProvider::BeginPlay()
{
	Super::BeginPlay();

	// OnRegister 가 아니라 BeginPlay 에서 한다.
	// 등록 순서가 보장되지 않아 OnRegister 시점에는 기존 SpringArm 을 못 찾고
	// 카메라를 하나 더 만들어버릴 수 있다(카메라 2개 → 뷰가 엉킨다).
	EnsureCamera();
}

void ULocalInputProvider::EnsureCamera()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 1) 이미 붙어 있으면 그걸 쓴다. 블루프린트에서 튜닝해둔 값을 잃지 않기 위해서.
	if (!CameraBoom)
	{
		CameraBoom = Owner->FindComponentByClass<USpringArmComponent>();
	}
	if (!FollowCamera)
	{
		FollowCamera = Owner->FindComponentByClass<UCameraComponent>();
	}

	if (CameraBoom && FollowCamera)
	{
		return;
	}

	// 2) 없으면 만든다.
	USceneComponent* Root = Owner->GetRootComponent();
	if (!Root)
	{
		return;
	}

	if (!CameraBoom)
	{
		CameraBoom = NewObject<USpringArmComponent>(Owner, TEXT("CameraBoom_Provider"));
		CameraBoom->SetupAttachment(Root);
		CameraBoom->TargetArmLength = ArmLength;
		CameraBoom->bUsePawnControlRotation = true;
		CameraBoom->RegisterComponent();
	}

	if (!FollowCamera)
	{
		FollowCamera = NewObject<UCameraComponent>(Owner, TEXT("FollowCamera_Provider"));
		FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
		FollowCamera->bUsePawnControlRotation = false;
		FollowCamera->SetFieldOfView(CameraFOV);
		FollowCamera->RegisterComponent();
	}
}

void ULocalInputProvider::BindInput(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogTurtleIsland, Error, TEXT("%s: Enhanced Input component 를 찾지 못했다."), *GetNameSafe(GetOwner()));
		return;
	}

	if (JumpAction)
	{
		EIC->BindAction(JumpAction, ETriggerEvent::Started,   this, &ULocalInputProvider::HandleJumpStart);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ULocalInputProvider::HandleJumpEnd);
	}

	if (MoveAction)
	{
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ULocalInputProvider::HandleMove);
		// 손을 떼면 Triggered 가 더 안 오므로 캐시를 직접 비워야 한다
		EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &ULocalInputProvider::HandleMoveEnd);
		EIC->BindAction(MoveAction, ETriggerEvent::Canceled,  this, &ULocalInputProvider::HandleMoveEnd);
	}

	if (LookAction)
	{
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ULocalInputProvider::HandleLook);
	}
	if (MouseLookAction)
	{
		EIC->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ULocalInputProvider::HandleLook);
	}
}

float ULocalInputProvider::GetViewYaw() const
{
	// 카메라 기준축 = 컨트롤 회전. 이동 방향을 만들 때 쓴 것과 같은 값이어야 한다.
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

FVector2D ULocalInputProvider::ToWorldDirection(const FVector2D& LocalAxis) const
{
	const FRotator YawRotation(0.f, GetViewYaw(), 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// LocalAxis.X = Right, LocalAxis.Y = Forward
	const FVector World = Forward * LocalAxis.Y + Right * LocalAxis.X;
	return FVector2D(World.X, World.Y);
}

void ULocalInputProvider::HandleMove(const FInputActionValue& Value)
{
	CachedWorldDirection = ToWorldDirection(Value.Get<FVector2D>());
}

void ULocalInputProvider::HandleMoveEnd(const FInputActionValue& Value)
{
	CachedWorldDirection = FVector2D::ZeroVector;
}

void ULocalInputProvider::HandleLook(const FInputActionValue& Value)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->GetController())
	{
		return;
	}

	const FVector2D LookAxis = Value.Get<FVector2D>();
	OwnerPawn->AddControllerYawInput(LookAxis.X);
	OwnerPawn->AddControllerPitchInput(LookAxis.Y);
}

void ULocalInputProvider::HandleJumpStart(const FInputActionValue& Value)
{
	OnJumpPressed.Broadcast();
}

void ULocalInputProvider::HandleJumpEnd(const FInputActionValue& Value)
{
	OnJumpReleased.Broadcast();
}

float ULocalInputProvider::GetFacingYaw() const
{
	// 로컬 폰은 CharacterMovementComponent 의 bOrientRotationToMovement 가 회전을 잡는다.
	// 값 자체는 참고용으로만 돌려준다 (WantsFacingOverride() == false).
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorRotation().Yaw;
	}
	return 0.f;
}
