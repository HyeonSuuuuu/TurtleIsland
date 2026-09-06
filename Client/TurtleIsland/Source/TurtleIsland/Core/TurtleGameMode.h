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
	 * 리스폰 "실행"은 여기 한 곳에만 있다. 트리거만 모드에 따라 갈린다 —
	 *   싱글 : ScheduleRespawn 이 타이머로 스스로 호출
	 *   멀티 : 서버의 SC_RESPAWN_NOTIFY 를 받아 NetSubsystem 이 호출 (패킷 설계 후 연결)
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



