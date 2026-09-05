// Copyright Epic Games, Inc. All Rights Reserved.


#include "Core/TurtlePlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Network/NetSubsystem.h"
#include "UI/PauseMenuWidget.h"
#include "TurtleIsland.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SVirtualJoystick.h"

void ATurtlePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 인게임 맵(Lv3_Ingame)에서만 게임 입력모드로 복귀.
	// 로비/방(UI 레벨)은 위젯이 FInputModeUIOnly + 커서를 쓰므로 건드리지 않는다.
	// (이 컨트롤러는 로비/방에서도 동일하게 쓰이므로 레벨 이름으로 한정)
	if (IsLocalPlayerController() && GetWorld() &&
		GetWorld()->GetMapName().Contains(TEXT("Lv3_Ingame")))
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);

		// 인게임 로딩 완료 → 서버에 현재 상태(다른 플레이어/NPC) 요청
		if (UGameInstance* GI = GetGameInstance())
		{
			UNetSubsystem* Net = GI->GetSubsystem<UNetSubsystem>();
			if (Net && !Net->IsSinglePlay())
			{
				FVector Loc = FVector::ZeroVector;
				float Yaw = 0.f;
				if (APawn* P = GetPawn())
				{
					Loc = P->GetActorLocation();
					Yaw = P->GetActorRotation().Yaw;
				}
				Net->NotifyIngameReady(Loc.X, Loc.Y, Yaw);
			}
		}
	}

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogTurtleIsland, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void ATurtlePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}

		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ATurtlePlayerController::TogglePauseMenu);
	}
}

void ATurtlePlayerController::TogglePauseMenu()
{
	if (PauseMenuWidget)
	{
		ClosePauseMenu();
		return;
	}

	// 이 컨트롤러는 로비/방에서도 쓰이므로 인게임에서만 연다
	if (!GetWorld() || !GetWorld()->GetMapName().Contains(UNetSubsystem::IngameLevelName.ToString()))
		return;

	if (!PauseMenuWidgetClass) return;

	PauseMenuWidget = CreateWidget<UPauseMenuWidget>(this, PauseMenuWidgetClass);
	if (!PauseMenuWidget) return;

	PauseMenuWidget->AddToViewport(100);

	SetShowMouseCursor(true);
	SetInputMode(FInputModeUIOnly());

	// 멀티에서 멈추면 서버 패킷만 쌓이므로 싱글에서만 일시정지
	if (UGameInstance* GI = GetGameInstance())
	{
		UNetSubsystem* Net = GI->GetSubsystem<UNetSubsystem>();
		if (Net && Net->IsSinglePlay())
			UGameplayStatics::SetGamePaused(this, true);
	}
}

void ATurtlePlayerController::ClosePauseMenu()
{
	if (!PauseMenuWidget) return;

	PauseMenuWidget->RemoveFromParent();
	PauseMenuWidget = nullptr;

	UGameplayStatics::SetGamePaused(this, false);

	SetShowMouseCursor(false);
	SetInputMode(FInputModeGameOnly());
}

bool ATurtlePlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
