// Copyright Epic Games, Inc. All Rights Reserved.


#include "Core/TurtlePlayerController.h"
#include "Core/TurtleAudioSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Network/NetSubsystem.h"
#include "UI/PauseMenuWidget.h"
#include "UI/ShopWidget.h"
#include "UI/MinimapWidget.h"
#include "UI/PlayerUIComponent.h"
#include "TurtleIsland.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SVirtualJoystick.h"

ATurtlePlayerController::ATurtlePlayerController()
{
	PlayerUIComponent = CreateDefaultSubobject<UPlayerUIComponent>(TEXT("PlayerUI"));
}

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

		// 인게임 진입 통지(NotifyIngameReady)는 UNetSyncComponent::BeginPlay 로 옮겼다.
		// 그 컴포넌트는 멀티에서만 붙으므로 여기서 IsSinglePlay() 를 물을 필요가 없어졌고,
		// 폰이 확실히 존재·배치된 시점이라 좌표도 더 정확하다.
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
		InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ATurtlePlayerController::ToggleWorldMap);
		for (const FKey& Key : {EKeys::Equals, EKeys::Add}) InputComponent->BindKey(Key, IE_Pressed, this, &ATurtlePlayerController::ZoomMinimapIn);
		for (const FKey& Key : {EKeys::Hyphen, EKeys::Subtract}) InputComponent->BindKey(Key, IE_Pressed, this, &ATurtlePlayerController::ZoomMinimapOut);
	}
}

void ATurtlePlayerController::TogglePauseMenu()
{
	if (PauseMenuWidget)
	{
		ClosePauseMenu();
		return;
	}

	// 전체 지도가 떠 있으면 먼저 닫는다.
	if (UMinimapWidget* Map = UMinimapWidget::FindFor(this); Map && Map->IsFullMap())
	{
		Map->ToggleFullMap();
		return;
	}

	// 상점이 열려 있으면 일시정지 메뉴 대신 상점을 닫는다(입력창 밖에 포커스가 있을 때).
	if (UShopWidget* Shop = UShopWidget::FindOpen(this))
	{
		Shop->Close();
		return;
	}

	// 이 컨트롤러는 로비/방에서도 쓰이므로 인게임에서만 연다
	if (!GetWorld() || !GetWorld()->GetMapName().Contains(UNetSubsystem::IngameLevelName.ToString()))
		return;

	if (!PauseMenuWidgetClass) return;

	PauseMenuWidget = CreateWidget<UPauseMenuWidget>(this, PauseMenuWidgetClass);
	if (!PauseMenuWidget) return;

	PauseMenuWidget->AddToViewport(100);
	UTurtleAudioSubsystem::Play(this, ETurtleSound::WindowOpen);

	if (PlayerUIComponent)
	{
		PlayerUIComponent->BeginUIInteraction(PauseMenuWidget, TEXT("Pause"));
	}

	// 멀티에서 멈추면 서버 패킷만 쌓이므로 싱글에서만 일시정지
	if (UGameInstance* GI = GetGameInstance())
	{
		UNetSubsystem* Net = GI->GetSubsystem<UNetSubsystem>();
		if (Net && Net->IsSinglePlay())
			UGameplayStatics::SetGamePaused(this, true);
	}
}

void ATurtlePlayerController::ToggleWorldMap()
{
	if (UMinimapWidget* Map = UMinimapWidget::FindFor(this)) Map->ToggleFullMap();
}

void ATurtlePlayerController::ZoomMinimapIn()
{
	if (UMinimapWidget* Map = UMinimapWidget::FindFor(this)) Map->Zoom(1);
}

void ATurtlePlayerController::ZoomMinimapOut()
{
	if (UMinimapWidget* Map = UMinimapWidget::FindFor(this)) Map->Zoom(-1);
}

void ATurtlePlayerController::ClosePauseMenu()
{
	if (!PauseMenuWidget) return;

	PauseMenuWidget->RemoveFromParent();
	UTurtleAudioSubsystem::Play(this, ETurtleSound::WindowClose);
	UPauseMenuWidget* ClosedWidget = PauseMenuWidget;
	PauseMenuWidget = nullptr;

	UGameplayStatics::SetGamePaused(this, false);

	if (PlayerUIComponent)
	{
		PlayerUIComponent->EndUIInteraction(ClosedWidget);
	}
	else
	{
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());
	}
}

bool ATurtlePlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
