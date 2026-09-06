// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/AgentInputComponent.h"
#include "LocalInputProvider.generated.h"

class UInputAction;
class UInputComponent;
class USpringArmComponent;
class UCameraComponent;
struct FInputActionValue;

/**
 * 키보드/마우스로 조종되는 로컬 플레이어용 입력 소스.
 *
 * 뷰 기준축을 여기가 소유한다 — 카메라 기준 축을 월드로 돌리는 변환을 삼키므로
 * Mover 는 컨트롤러도 카메라도 몰라도 된다.
 * 카메라 컴포넌트도 여기가 책임진다. 소유 액터에 이미 SpringArm 이 있으면 그걸 채택하고
 * (블루프린트에서 튜닝해둔 값을 지키려고), 없으면 런타임에 만든다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API ULocalInputProvider : public UAgentInputComponent
{
	GENERATED_BODY()

public:
	ULocalInputProvider();

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction = nullptr;

	/** 소유 액터에 카메라가 없을 때 만들 스프링암 길이 */
	UPROPERTY(EditAnywhere, Category="Camera")
	float ArmLength = 400.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float CameraFOV = 90.f;

	/** 폰의 SetupPlayerInputComponent 에서 불러준다 */
	void BindInput(UInputComponent* PlayerInputComponent);

	/**
	 * 예전에 캐릭터 클래스에 물려두었던 InputAction 에셋을 넘겨받는다.
	 * 비어 있는 슬롯만 채우므로, 블루프린트를 리페어런팅하지 않아도 기존 세팅이 그대로 산다.
	 */
	void AdoptLegacyActions(UInputAction* InMove, UInputAction* InLook, UInputAction* InMouseLook, UInputAction* InJump);

	/** 모바일 가상 조이스틱 등 Enhanced Input 밖에서 들어오는 입력용 (Right, Forward) */
	void InjectMoveAxis(const FVector2D& LocalAxis);
	void InjectLookAxis(const FVector2D& LookAxis);

	virtual FVector2D GetMovementInput() const override { return CachedWorldDirection; }
	virtual float GetFacingYaw() const override;
	virtual bool WantsFacingOverride() const override { return false; }

	UFUNCTION(BlueprintPure, Category="Camera")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	UFUNCTION(BlueprintPure, Category="Camera")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

protected:
	virtual void BeginPlay() override;

	void HandleMove(const FInputActionValue& Value);
	void HandleMoveEnd(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpStart(const FInputActionValue& Value);
	void HandleJumpEnd(const FInputActionValue& Value);

	/** 이미 있으면 채택, 없으면 생성 */
	void EnsureCamera();

	/** 컨트롤 회전 기준 축(Right, Forward) 을 월드 XY 방향으로 */
	FVector2D ToWorldDirection(const FVector2D& LocalAxis) const;

private:
	UPROPERTY(Transient)
	USpringArmComponent* CameraBoom = nullptr;

	UPROPERTY(Transient)
	UCameraComponent* FollowCamera = nullptr;

	/** 월드 XY 방향. Enhanced Input 의 Triggered/Completed 로 갱신된다 */
	FVector2D CachedWorldDirection = FVector2D::ZeroVector;
};
