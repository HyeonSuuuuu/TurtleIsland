// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TurtleIslandPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class ATurtleIslandPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	/** ESC 메뉴 토글 (인게임에서만 동작) */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePauseMenu();

	/** ESC 메뉴를 닫고 게임 입력으로 복귀 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ClosePauseMenu();

	/** 엔터: 채팅 입력창에 포커스를 준다 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void OpenChat();

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

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

	/**
	 *  화면에 떠 있는 메인 HUD를 찾는다(없으면 nullptr).
	 *  HUD는 블루프린트에서 생성하므로 컨트롤러가 직접 들고 있지 않다.
	 *  매번 훑지 않도록 찾은 뒤 캐시한다.
	 */
	class UIngameHUDWidget* GetIngameHUD();

	/** GetIngameHUD 캐시 */
	UPROPERTY(Transient)
	TObjectPtr<class UIngameHUDWidget> CachedHUDWidget;

};
