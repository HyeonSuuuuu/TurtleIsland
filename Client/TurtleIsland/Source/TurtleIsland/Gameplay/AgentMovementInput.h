// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentMovementInput.generated.h"

/**
 * Agent 를 무엇이 조종하는지를 추상화한 베이스.
 * 로컬 플레이어(키보드/마우스)든 서버 패킷이든 이 인터페이스 하나로 수렴한다.
 *
 * 좌표계 규약: GetMovementInput() 은 반드시 "월드 XY" 기준이다.
 * 카메라 기준 축을 월드로 돌리는 변환은 입력 쪽 관심사라 구현체가 삼킨다.
 * 덕분에 Mover 는 컨트롤러를 몰라도 된다.
 */
UCLASS(Abstract, ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UAgentMovementInput : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentMovementInput();

	/** 월드 XY 기준 이동 방향. 길이 = 아날로그 강도 0~1 */
	UFUNCTION(BlueprintPure, Category="Agent|Input")
	virtual FVector2D GetMovementInput() const { return FVector2D::ZeroVector; }

	/** 바라볼 월드 yaw. 제자리 회전이 있어 이동 방향과 분리한다 */
	UFUNCTION(BlueprintPure, Category="Agent|Input")
	virtual float GetFacingYaw() const { return 0.f; }

	/** true 면 Mover 가 GetFacingYaw() 로 강제 회전. false 면 이동 방향을 따른다 */
	virtual bool WantsFacingOverride() const { return false; }

	/**
	 * GetMovementInput() 을 만들 때 기준으로 삼은 월드 yaw.
	 * 이 값을 아는 건 방향을 만든 쪽뿐이라, 필요한 곳이 다시 유도하지 않게 여기서 내준다.
	 */
	UFUNCTION(BlueprintPure, Category="Agent|Input")
	virtual float GetViewYaw() const;


	/** 점프는 상태가 아니라 이벤트라 Intent 에 넣지 않는다 */
	DECLARE_MULTICAST_DELEGATE(FAgentJumpSignal);
	FAgentJumpSignal OnJumpPressed;
	FAgentJumpSignal OnJumpReleased;
};
