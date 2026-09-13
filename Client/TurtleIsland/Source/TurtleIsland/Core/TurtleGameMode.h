// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TurtleGameMode.generated.h"

class ATurtleAgent;

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class ATurtleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	ATurtleGameMode();

	/**
	 * 싱글 리스폰 진입점. 멀티는 NetSubsystem 이 서버 통지를 처리한다.
	 *   싱글 : ScheduleRespawn 이 타이머로 스스로 호출
	 *   멀티 : 서버의 SC_RESPAWN_NOTIFY 를 받아 NetSubsystem 이 Agent 상태를 복구
	 *
	 * 새 폰을 스폰하지 않는다. 상대 Agent 는 NetSubsystem 의 players 맵이 붙들고 있어서
	 * Destroy 하면 맵이 깨지므로, 기존 폰의 상태만 되돌린다.
	 */
	UFUNCTION(BlueprintCallable, Category="Respawn")
	void RespawnAgent(ATurtleAgent* Agent, const FVector& At, int32 HP);

	/**
	 * 사망한 Agent 의 리스폰을 예약한다.
	 * 멀티에서는 서버가 시점을 정하므로 아무것도 예약하지 않는다 —
	 * 각 클라가 제 시계로 부활시키면 화면마다 부활 시점이 어긋난다.
	 */
	void ScheduleRespawn(ATurtleAgent* Agent, float Delay);
};



