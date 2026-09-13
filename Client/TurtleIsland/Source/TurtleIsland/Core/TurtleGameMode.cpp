// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/TurtleGameMode.h"
#include "Character/TurtleAgent.h"
#include "Network/NetSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATurtleGameMode::ATurtleGameMode()
{
	// stub
}

void ATurtleGameMode::ScheduleRespawn(ATurtleAgent* Agent, float Delay)
{
	if (!IsValid(Agent) || !GetWorld())
	{
		return;
	}

	// 멀티에서는 서버가 리스폰 시점을 정한다. 여기서 타이머를 걸면
	// 클라마다 제 시계로 부활해 화면이 어긋난다.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UNetSubsystem* Net = GI->GetSubsystem<UNetSubsystem>())
		{
			if (!Net->IsSinglePlay())
			{
				return;
			}
		}
	}

	TWeakObjectPtr<ATurtleAgent> WeakAgent(Agent);
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle, [this, WeakAgent]()
	{
		if (ATurtleAgent* Alive = WeakAgent.Get())
		{
			RespawnAgent(Alive, Alive->GetActorLocation(), 0);
		}
	}, FMath::Max(0.01f, Delay), false);
}

void ATurtleGameMode::RespawnAgent(ATurtleAgent* Agent, const FVector& At, int32 HP)
{
	if (!IsValid(Agent))
	{
		return;
	}

	Agent->ResetForRespawn(At, HP);
}
