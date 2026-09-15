// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TurtlePlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UPlayerUIComponent;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class ATurtlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATurtlePlayerController();

	/** ESC 메뉴 토글 (인게임에서만 동작) */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePauseMenu();

	/** ESC 메뉴를 닫고 게임 입력으로 복귀 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ClosePauseMenu();

	/** M: 전체 지도 토글, +/-: 미니맵 확대·축소 */
	void ToggleWorldMap();
	void ZoomMinimapIn();
	void ZoomMinimapOut();

protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** ESC로 띄울 일시정지 메뉴 위젯 (WBP_PauseMenu) */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<class UPauseMenuWidget> PauseMenuWidgetClass;

	/** 현재 떠 있는 일시정지 메뉴 */
	UPROPERTY()
	TObjectPtr<class UPauseMenuWidget> PauseMenuWidget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UPlayerUIComponent> PlayerUIComponent;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

};
