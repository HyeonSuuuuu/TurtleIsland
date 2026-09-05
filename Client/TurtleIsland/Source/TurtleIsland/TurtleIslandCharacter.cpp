// Copyright Epic Games, Inc. All Rights Reserved.

#include "TurtleIslandCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "TurtleIsland.h"
#include "NetSubsystem.h"
#include "PlayerStatsComponent.h"
#include "Protocol.h"

ATurtleIslandCharacter::ATurtleIslandCharacter()
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

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Stats = CreateDefaultSubobject<UPlayerStatsComponent>(TEXT("Stats"));

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void ATurtleIslandCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATurtleIslandCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ATurtleIslandCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATurtleIslandCharacter::Look);
	}
	else
	{
		UE_LOG(LogTurtleIsland, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void ATurtleIslandCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	LastMoveInputTime += DeltaTime;

	if (LastMoveInputTime > PacketSendInterval)
	{
		LastMoveInputTime -= PacketSendInterval;
		if (InputFlag > 0)
		{
			UNetSubsystem* NetSubsystem = GetGameInstance()->GetSubsystem<UNetSubsystem>();
			if (NetSubsystem && !NetSubsystem->IsSinglePlay())
			{
				// 캐릭터 실제 facing (이동 방향 회전 포함)
				float CurrentYaw = GetActorRotation().Yaw;

				CS_Move MovePacket{};
				MovePacket.size      = CS_MOVE_PACKET_SIZE;
				MovePacket.id        = (uint16)PACKET_ID::CS_MOVE;
				MovePacket.inputFlag = InputFlag;
				MovePacket.yaw       = CurrentYaw;

				// 실제 위치 함께 전송
				const FVector Loc = GetActorLocation();
				MovePacket.x = Loc.X;
				MovePacket.y = Loc.Y;

				NetSubsystem->SendPacket(MovePacket);
			}

			// 싱글 모드에서도 매 주기 비워준다(누적 방지)
			InputFlag = 0;
		}
	}
}

void ATurtleIslandCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void ATurtleIslandCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void ATurtleIslandCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);

		// 서버로 보낼 InputFlag
		if (Forward > 0.1f) InputFlag |= 1; // W
		if (Right < -0.1f) InputFlag |= 2; // A
		if (Forward < -0.1f)   InputFlag |= 4; // S
		if (Right > 0.1f)    InputFlag |= 8; // D
	}
}

void ATurtleIslandCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ATurtleIslandCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void ATurtleIslandCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

// ---------------------------------------------------------------------------
// 피격/사망 처리. 싱글 전투(Variant_Combat)가 빠지면서 호출자가 없다.
// 서버 전투를 붙이면 SC_ 패킷 핸들러가 여기를 호출하게 된다.
// ---------------------------------------------------------------------------

void ATurtleIslandCharacter::BeginPlay()
{
	CurrentHP = MaxHP;
	bIsDead = false;

	// 몬스터 공격 트레이스가 태그로 대상을 거르므로 코드에서도 보장
	if (!ActorHasTag(TEXT("Player")))
	{
		Tags.Add(TEXT("Player"));
	}

	Super::BeginPlay();
}

void ATurtleIslandCharacter::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	if (bIsDead || Damage <= 0.f)
	{
		return;
	}

	CurrentHP = FMath::Max(0.f, CurrentHP - Damage);

	OnDamaged(Damage, GetHPRatio(), DamageCauser);

	if (CurrentHP <= 0.f)
	{
		HandleDeath();
		return;
	}

	// 넉백
	if (!DamageImpulse.IsNearlyZero())
	{
		LaunchCharacter(DamageImpulse, true, true);
	}
}

void ATurtleIslandCharacter::HandleDeath()
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;

	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (AController* C = GetController())
	{
		C->SetIgnoreMoveInput(true);
		C->SetIgnoreLookInput(true);
	}

	OnDied();

	// 일정 시간 뒤 체력만 복구(리스폰 연출은 블루프린트 OnDied 에서 처리)
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, [this]()
	{
		if (!IsValid(this))
		{
			return;
		}
		CurrentHP = MaxHP;
		bIsDead = false;
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		if (AController* C = GetController())
		{
			C->SetIgnoreMoveInput(false);
			C->SetIgnoreLookInput(false);
		}
	}, DeathDisableTime, false);
}

void ATurtleIslandCharacter::ApplyHealing(float Healing, AActor* Healer)
{
	if (bIsDead || Healing <= 0.f)
	{
		return;
	}
	CurrentHP = FMath::Min(MaxHP, CurrentHP + Healing);
}

