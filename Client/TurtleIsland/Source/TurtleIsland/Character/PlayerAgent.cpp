// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/PlayerAgent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Gameplay/LocalInputProvider.h"
#include "Gameplay/LocalMoverComponent.h"
#include "Gameplay/PlayerStatsComponent.h"
#include "Network/NetSubsystem.h"
#include "Network/NetSyncComponent.h"
#include "TurtleIsland.h"

APlayerAgent::APlayerAgent()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// 카메라는 여기서 만든다. 블루프린트에 튜닝해둔 값을 지키려고 남긴 것이고,
	// ULocalInputProvider 가 BeginPlay 에서 이걸 채택해 쓴다.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Stats = CreateDefaultSubobject<UPlayerStatsComponent>(TEXT("Stats"));

	// 로컬 플레이어용 컴포넌트 세트
	LocalInput = CreateDefaultSubobject<ULocalInputProvider>(TEXT("LocalInput"));
	Mover      = CreateDefaultSubobject<ULocalMoverComponent>(TEXT("Mover"));

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character)
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void APlayerAgent::BeginPlay()
{
	// 몬스터 공격 트레이스가 태그로 대상을 거르므로 코드에서도 보장
	if (!ActorHasTag(TEXT("Player")))
	{
		Tags.Add(TEXT("Player"));
	}

	Super::BeginPlay();

	// 싱글/멀티 분기는 여기 한 번뿐이다. 멀티일 때만 서버 송신 컴포넌트를 붙인다.
	const UGameInstance* GI = GetGameInstance();
	const UNetSubsystem* Net = GI ? GI->GetSubsystem<UNetSubsystem>() : nullptr;
	if (Net && !Net->IsSinglePlay())
	{
		UNetSyncComponent* Sync = NewObject<UNetSyncComponent>(this, TEXT("NetSync"));
		Sync->RegisterComponent();
	}
}

void APlayerAgent::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (!LocalInput)
	{
		UE_LOG(LogTurtleIsland, Error, TEXT("%s: ULocalInputProvider 가 없다."), *GetNameSafe(this));
		return;
	}

	// 블루프린트에 남아 있는 InputAction 에셋을 Provider 로 넘긴다. 비어 있는 슬롯만 채운다.
	LocalInput->AdoptLegacyActions(MoveAction, LookAction, MouseLookAction, JumpAction);
	LocalInput->BindInput(PlayerInputComponent);
}

void APlayerAgent::DoMove(float Right, float Forward)
{
	if (LocalInput)
	{
		LocalInput->InjectMoveAxis(FVector2D(Right, Forward));
	}
}

void APlayerAgent::DoLook(float Yaw, float Pitch)
{
	if (LocalInput)
	{
		LocalInput->InjectLookAxis(FVector2D(Yaw, Pitch));
	}
}

void APlayerAgent::DoJumpStart()
{
	Jump();
}

void APlayerAgent::DoJumpEnd()
{
	StopJumping();
}
