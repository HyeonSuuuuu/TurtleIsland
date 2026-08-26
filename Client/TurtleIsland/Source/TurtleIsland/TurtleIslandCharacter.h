// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "CombatDamageable.h"
#include "TurtleIslandCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UPlayerStatsComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class ATurtleIslandCharacter : public ACharacter, public ICombatDamageable
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
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Constructor */
	ATurtleIslandCharacter();	

protected:

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;
	float LastMoveInputTime = 0.f;
	const float PacketSendInterval = 1.f / 30.f;
	uint8 InputFlag = 0;
protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

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

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	/** 최대 체력 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage")
	float MaxHP = 100.f;

	/** 현재 체력. BeginPlay 에서 MaxHP 로 채운다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Damage")
	float CurrentHP = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Damage")
	bool bIsDead = false;

	/** 사망 후 조작 불가 상태로 남기는 시간(초) */
	UPROPERTY(EditAnywhere, Category="Damage")
	float DeathDisableTime = 3.f;

	/** 피격 연출 — 블루프린트에서 구현 */
	UFUNCTION(BlueprintImplementableEvent, Category="Damage")
	void OnDamaged(float Damage, float HPRatio, AActor* Causer);

	/** 사망 연출 — 블루프린트에서 구현 */
	UFUNCTION(BlueprintImplementableEvent, Category="Damage")
	void OnDied();

	/** 체력 비율 0~1 */
	UFUNCTION(BlueprintPure, Category="Damage")
	float GetHPRatio() const { return MaxHP > 0.f ? FMath::Clamp(CurrentHP / MaxHP, 0.f, 1.f) : 0.f; }

	/** 스탯 컴포넌트 (몬스터 보상 지급/HUD 바인딩용) */
	UFUNCTION(BlueprintPure, Category="Stats")
	UPlayerStatsComponent* GetStats() const { return Stats; }

	// --- ICombatDamageable ---
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;
	virtual void HandleDeath() override;
	virtual void ApplyHealing(float Healing, AActor* Healer) override;
	virtual void NotifyDanger(const FVector& DangerLocation, AActor* DangerSource) override;

protected:

	virtual void BeginPlay() override;

public:
	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

