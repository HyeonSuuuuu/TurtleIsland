// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GroundDetector.generated.h"

/**
 * 아래로 라인 트레이스를 쏴서 지면 Z 를 찾는다.
 *
 * 주의 — 이건 CharacterMovementComponent 를 "끈" 폰(원격 프록시) 전용이다. 로컬 폰에는 붙이지 말 것.
 * UCharacterMovementComponent 가 FindFloor/CurrentFloor/step-up 을 이미 하고 있어서,
 * 접지 판정이 두 벌이 되면 서로 어긋나는 순간 재현이 까다로운 버그가 된다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UGroundDetector : public UActorComponent
{
	GENERATED_BODY()

public:
	UGroundDetector();

	/** 트레이스 시작점을 대상 위치에서 얼마나 위로 올릴지 */
	UPROPERTY(EditAnywhere, Category="Ground")
	float TraceUp = 500.f;

	/** 얼마나 아래까지 훑을지 */
	UPROPERTY(EditAnywhere, Category="Ground")
	float TraceDown = 3000.f;

	/** 트레이스가 아무것도 못 맞췄을 때 쓸 Z. 기존 하드코딩 값과 동일 */
	UPROPERTY(EditAnywhere, Category="Ground")
	float FallbackZ = 92.f;

	/**
	 * XY 를 받아 액터 루트가 놓일 Z 를 돌려준다.
	 * 캡슐 절반 높이를 더해주므로 결과를 그대로 SetActorLocation 에 쓰면 된다.
	 */
	UFUNCTION(BlueprintCallable, Category="Ground")
	float ResolveActorZ(float WorldX, float WorldY, float SearchAroundZ) const;

protected:
	/** 소유 액터가 ACharacter 면 캡슐 절반 높이, 아니면 0 */
	float GetFootOffset() const;
};
