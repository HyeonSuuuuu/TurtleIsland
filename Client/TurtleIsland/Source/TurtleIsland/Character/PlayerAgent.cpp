// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/PlayerAgent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Gameplay/PlayerInputComponent.h"
#include "Gameplay/LocalMovementInput.h"
#include "Gameplay/InteractionComponent.h"
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

	Stats = CreateDefaultSubobject<UPlayerStatsComponent>(TEXT("Stats"));

	// 로컬 플레이어용 컴포넌트 세트
	// Camera orientation is independent of character facing and mouse movement.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	ConfigureTopDownCamera();

	Mover = CreateDefaultSubobject<ULocalMoverComponent>(TEXT("Mover"));

	// 입력 부품들. 서로 델리게이트로 붙으므로 Agent 가 중개하지 않는다.
	PlayerInput = CreateDefaultSubobject<UPlayerInputComponent>(TEXT("PlayerInput"));
	LocalInput  = CreateDefaultSubobject<ULocalMovementInput>(TEXT("LocalInput"));
	Interaction = CreateDefaultSubobject<UInteractionComponent>(TEXT("Interaction"));

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character)
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void APlayerAgent::ConfigureTopDownCamera()
{
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->SetRelativeRotation(FRotator(TopDownPitch, TopDownYaw, 0.f));
	CameraBoom->TargetArmLength = TopDownDistance;
	CameraBoom->TargetOffset = FVector(0.f, 0.f, 60.f);
	CameraBoom->SocketOffset = FVector::ZeroVector;
	CameraBoom->bDoCollisionTest = false;
	FollowCamera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(55.f);
}

float APlayerAgent::GetCameraViewYaw() const
{
	return CameraBoom->GetTargetRotation().Yaw;
}

void APlayerAgent::BeginPlay()
{
	// 몬스터 공격 트레이스가 태그로 대상을 거르므로 코드에서도 보장
	if (!ActorHasTag(TEXT("Player")))
	{
		Tags.Add(TEXT("Player"));
	}

	Super::BeginPlay();
	// Apply after Blueprint defaults so older third-person component overrides cannot win.
	ConfigureTopDownCamera();

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
	// Agent 가 입력에 대해 하는 일은 이게 전부다.
	if (UPlayerInputComponent* Binder = FindComponentByClass<UPlayerInputComponent>())
	{
		Binder->BindInput(PlayerInputComponent);
	}
	else
	{
		UE_LOG(LogTurtleIsland, Error, TEXT("%s: UPlayerInputComponent 가 없다."), *GetNameSafe(this));
	}
}
