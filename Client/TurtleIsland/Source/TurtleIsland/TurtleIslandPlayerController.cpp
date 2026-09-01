// Copyright Epic Games, Inc. All Rights Reserved.


#include "TurtleIslandPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "_Turtle/Network/NetSubsystem.h"
#include "_Turtle/UI/PauseMenuWidget.h"
#include "_Turtle/UI/IngameHUDWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "TurtleIsland.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SVirtualJoystick.h"

void ATurtleIslandPlayerController::BeginPlay()
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

void ATurtleIslandPlayerController::SetupInputComponent()
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

		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ATurtleIslandPlayerController::TogglePauseMenu);

		// 채팅 열기. 채팅 중에는 EditableText가 포커스를 갖고 엔터를 소비하므로
		// 여기까지 안 온다 — 그래서 "열기"만 있으면 되고 토글이 아니다.
		InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ATurtleIslandPlayerController::OpenChat);
	}
}

UIngameHUDWidget* ATurtleIslandPlayerController::GetIngameHUD()
{
	if (IsValid(CachedHUDWidget) && CachedHUDWidget->IsInViewport())
	{
		return CachedHUDWidget;
	}

	CachedHUDWidget = nullptr;

	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UIngameHUDWidget::StaticClass(), true);
	if (Found.Num() > 0)
	{
		CachedHUDWidget = Cast<UIngameHUDWidget>(Found[0]);
	}
	return CachedHUDWidget;
}

void ATurtleIslandPlayerController::OpenChat()
{
	// ESC 메뉴가 떠 있으면 채팅을 열지 않는다
	if (PauseMenuWidget) return;

	if (UIngameHUDWidget* HUD = GetIngameHUD())
	{
		HUD->FocusChat();
	}
}

void ATurtleIslandPlayerController::TogglePauseMenu()
{
	// 채팅 중이면 ESC는 채팅만 닫는다. 메뉴까지 뜨면 두 개가 겹친다.
	// (보통은 위젯이 포커스를 갖고 있어 여기까지 안 오지만, 포커스를 잃은 채
	//  채팅이 열려 있는 경우를 대비한 방어선)
	if (UIngameHUDWidget* HUD = GetIngameHUD())
	{
		if (HUD->IsChatFocused())
		{
			HUD->CloseChat(false);
			return;
		}
	}

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

void ATurtleIslandPlayerController::ClosePauseMenu()
{
	if (!PauseMenuWidget) return;

	PauseMenuWidget->RemoveFromParent();
	PauseMenuWidget = nullptr;

	UGameplayStatics::SetGamePaused(this, false);

	SetShowMouseCursor(false);
	SetInputMode(FInputModeGameOnly());
}

bool ATurtleIslandPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
