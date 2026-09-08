// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/AgentMoverComponent.h"
#include "LocalMoverComponent.generated.h"

/**
 * 로컬 플레이어용. CharacterMovementComponent 에 그대로 위임한다.
 *
 * CharacterMovementComponent 를 대체하지 않는다 — 접지 판정, 슬로프, step-up 은
 * 이미 CharacterMovementComponent 가 다 하고 있으므로 여기서는 방향만 먹여준다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API ULocalMoverComponent : public UAgentMoverComponent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	void HandleJumpPressed();
	void HandleJumpReleased();

public:
	ULocalMoverComponent();


	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
