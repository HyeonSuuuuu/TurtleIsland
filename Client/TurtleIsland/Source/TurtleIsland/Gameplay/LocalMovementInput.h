// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/AgentMovementInput.h"
#include "LocalMovementInput.generated.h"

/**
 * 키보드/마우스로 조종되는 로컬 플레이어의 이동 의도 소스.
 *
 * 키는 직접 바인딩하지 않는다. UPlayerInputComponent 를 구독해서 축 값만 받고,
 * 뷰 기준 축을 월드 XY 로 돌리는 변환을 여기서 삼킨다.
 * 그래서 Mover 는 컨트롤러도 카메라도 몰라도 된다.
 *
 * 기준축은 컨트롤 회전이지 카메라가 아니다. 그래서 카메라 컴포넌트는 여기 두지 않는다
 * (한때 여기서 소유했는데, 기준축과 무관하다는 게 드러나 폰으로 되돌렸다).
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API ULocalMovementInput : public UAgentMovementInput
{
	GENERATED_BODY()

public:
	ULocalMovementInput();

	virtual FVector2D GetMovementInput() const override { return CachedWorldDirection; }
	virtual float GetFacingYaw() const override;
	virtual float GetViewYaw() const override;
	virtual bool WantsFacingOverride() const override { return false; }

protected:
	virtual void BeginPlay() override;

	void HandleMoveAxis(const FVector2D& LocalAxis);
	void HandleMoveReleased();
	void HandleLookAxis(const FVector2D& LookAxis);
	void HandleJumpPressed();
	void HandleJumpReleased();

	/** 컨트롤 회전 기준 축(Right, Forward) 을 월드 XY 방향으로 */
	FVector2D ToWorldDirection(const FVector2D& LocalAxis) const;

private:
	/** 월드 XY 방향. 키를 놓으면 0 으로 돌아간다 */
	FVector2D CachedWorldDirection = FVector2D::ZeroVector;
};
