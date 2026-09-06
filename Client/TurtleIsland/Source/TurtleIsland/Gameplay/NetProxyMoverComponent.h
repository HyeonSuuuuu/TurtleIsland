// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/AgentMoverComponent.h"
#include "NetProxyMoverComponent.generated.h"

class UNetInputProvider;

/**
 * 원격 프록시(상대 플레이어 / NPC)용. 서버가 준 위치로 보간한다.
 *
 * CharacterMovementComponent 를 꺼버리므로 접지 판정도 같이 죽는다. 그래서 Z 는 GroundDetector 가 채운다
 * (UNetInputProvider 가 목표 위치를 만들 때 이미 해결해서 넘겨준다).
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UNetProxyMoverComponent : public UAgentMoverComponent
{
	GENERATED_BODY()

public:
	UNetProxyMoverComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 위치/회전 보간 속도 */
	UPROPERTY(EditAnywhere, Category="Net")
	float InterpSpeed = 12.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	UNetInputProvider* NetInput = nullptr;
};
