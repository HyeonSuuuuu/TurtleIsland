// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentMoverComponent.generated.h"

class UAgentInputComponent;

/**
 * 입력 소스가 준 "이동 방향"을 실제 이동으로 옮기는 베이스.
 *
 * 로컬 폰은 CharacterMovementComponent 에 위임하고(ULocalMoverComponent),
 * 원격 프록시는 서버 위치로 보간한다(UNetProxyMoverComponent).
 * 어느 쪽이든 컨트롤러나 카메라를 알 필요가 없다 — 방향은 이미 월드 XY 로 들어온다.
 */
UCLASS(Abstract, ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UAgentMoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentMoverComponent();

	/** 애님 블루프린트가 읽는 현재 평면 속도 */
	UFUNCTION(BlueprintPure, Category="Agent|Move")
	float GetSpeed2D() const { return CurrentSpeed2D; }

protected:
	virtual void BeginPlay() override;

	UAgentInputComponent* GetInput() const { return Input; }

	UPROPERTY(Transient)
	UAgentInputComponent* Input = nullptr;

	float CurrentSpeed2D = 0.f;
};
