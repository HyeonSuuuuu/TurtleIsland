// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "PauseMenuWidget.generated.h"

/**
 *  인게임 ESC 메뉴. 계속하기 / 타이틀로 / 게임 종료.
 *  열고 닫는 것은 ATurtlePlayerController가 담당한다.
 */
UCLASS()
class TURTLEISLAND_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 메뉴를 닫고 게임 입력으로 복귀(컨트롤러 경유)
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void Close();

protected:
	virtual void NativeConstruct() override;

	// 메뉴가 포커스를 가진 동안에는 컨트롤러 입력이 안 오므로 ESC를 여기서 받는다
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ResumeButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* TitleButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* QuitButton;

	UFUNCTION()
	void OnResumeClicked();

	UFUNCTION()
	void OnTitleClicked();

	UFUNCTION()
	void OnQuitClicked();
};
