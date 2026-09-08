// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/TurtleAgent.h"
#include "ServerAgent.generated.h"

class UNetMovementInput;
class UNetProxyMoverComponent;
class UGroundDetector;

/**
 *  서버가 조종하는 Agent 의 조립 클래스 — 상대 플레이어, NPC, 몬스터가 모두 이 구성이다.
 *
 *  보간과 접지 판정 로직은 전부 컴포넌트로 나갔다. 여기 남은 일은 컴포넌트 세트를 붙이는 것뿐이다.
 *
 *  BP_Opponent 가 이 클래스를 상속하고 있어 남겨둔다. 블루프린트를 ATurtleAgent 로
 *  리페어런팅한 뒤에는 이 클래스를 지워도 된다.
 */
UCLASS()
class TURTLEISLAND_API AServerAgent : public ATurtleAgent
{
	GENERATED_BODY()

public:
	AServerAgent();

	/** 서버가 보낸 목표 상태를 입력 컴포넌트에 넘긴다. NetSubsystem 이 패킷마다 부른다 */
	void UpdateNetState(float WorldX, float WorldY, float Yaw);

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UNetMovementInput* NetInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UNetProxyMoverComponent* Mover;

	/** CharacterMovementComponent 가 꺼진 폰이라 Z 를 직접 구해야 한다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UGroundDetector* Ground;
};
