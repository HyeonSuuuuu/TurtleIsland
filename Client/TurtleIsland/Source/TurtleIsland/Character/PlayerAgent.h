// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Character/TurtleAgent.h"
#include "PlayerAgent.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UPlayerStatsComponent;
class ULocalInputProvider;
class ULocalMoverComponent;


/**
 *  로컬 플레이어가 조종하는 Agent 의 조립 클래스.
 *
 *  로직은 전부 컴포넌트로 나갔다. 여기 남은 일은 두 가지뿐 —
 *   1) 로컬 플레이어용 컴포넌트 세트를 붙인다
 *   2) 싱글/멀티 분기를 BeginPlay 에서 딱 한 번 본다 (NetSync 를 붙일지 말지)
 *
 *  BP_MyCharacter 가 이 클래스를 상속하고 있어 남겨둔다. 블루프린트를 ATurtleAgent 로
 *  리페어런팅한 뒤에는 이 클래스를 지워도 된다.
 */
UCLASS(abstract)
class APlayerAgent : public ATurtleAgent
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	/** 레벨/경험치/골드 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats", meta = (AllowPrivateAccess = "true"))
	UPlayerStatsComponent* Stats;

	/** 키보드/마우스 입력 소스 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	ULocalInputProvider* LocalInput;

	/** CharacterMovementComponent 위임 Mover */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	ULocalMoverComponent* Mover;

protected:

	/**
	 * InputAction 에셋 — 원래 자리가 여기라 블루프린트에 물려둔 값을 지키기 위해 남긴다.
	 * SetupPlayerInputComponent 에서 ULocalInputProvider 로 넘긴다.
	 * 새 세팅은 Provider 쪽에 직접 물리면 되고, 그때 이 슬롯들은 비워두면 된다.
	 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Constructor */
	APlayerAgent();

protected:

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump released inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** 스탯 컴포넌트 (몬스터 보상 지급/HUD 바인딩용) */
	UFUNCTION(BlueprintPure, Category="Stats")
	UPlayerStatsComponent* GetStats() const { return Stats; }

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};
