// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/AgentMovementInput.h"
#include "NetMovementInput.generated.h"

class UGroundDetector;

/**
 * 서버가 조종하는 폰(상대 플레이어 / NPC / 몬스터)의 입력 소스.
 *
 * SC_MOVE_NOTIFY 와 SC_NPC_MOVE 는 구조가 같아서 둘 다 이 하나로 덮인다.
 * 서버는 2D (x, y) 만 보내므로 Z 는 GroundDetector 로 클라가 직접 구한다
 * (기존의 OpponentGroundZ = 92.f 하드코딩을 대체).
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UNetMovementInput : public UAgentMovementInput
{
	GENERATED_BODY()

private:
	FVector TargetLocation = FVector::ZeroVector;
	float   TargetYaw      = 0.f;
	bool    bHasTarget     = false;
	bool bPlayerPose = false;
	bool bAirborne = false;
	float VerticalVelocity = 0.f;

	UPROPERTY(Transient)
	UGroundDetector* Ground = nullptr;

protected:
	virtual void BeginPlay() override;

	/** 소유 액터의 GroundDetector 로 Z 를 채운다. 없으면 현재 Z 유지 */
	float ResolveZ(float WorldX, float WorldY) const;

public:
	UNetMovementInput();

	/** 서버가 보낸 목표 상태. NetSubsystem 이 패킷마다 부른다 */
	void UpdateNetState(float WorldX, float WorldY, float Yaw);
	void UpdatePlayerState(const FVector& Position, float Yaw, float VelocityZ, bool bInAir);
	bool HasPlayerPose() const { return bPlayerPose; }
	bool IsAirborne() const { return bAirborne; }
	float GetVerticalVelocity() const { return VerticalVelocity; }

	virtual FVector2D GetMovementInput() const override;
	virtual float GetFacingYaw() const override { return TargetYaw; }

	/** 서버 yaw 가 이동 방향보다 우선한다 (제자리 회전이 있으므로) */
	virtual bool WantsFacingOverride() const override { return true; }

	bool HasTarget() const { return bHasTarget; }
	FVector GetTargetLocation() const { return TargetLocation; }

	/** 이 거리 안이면 도착으로 보고 이동 입력을 0 으로 */
	UPROPERTY(EditAnywhere, Category="Net")
	float ArriveTolerance = 2.f;
};
