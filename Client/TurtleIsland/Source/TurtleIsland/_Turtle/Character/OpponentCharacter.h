// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "OpponentCharacter.generated.h"

UCLASS()
class TURTLEISLAND_API AOpponentCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AOpponentCharacter();

	// 2. 모든 상대방의 데이터를 관리하는 TMap (정적 멤버 혹은 서브시스템 권장)
	// Key: PlayerID (uint32), Value: PlayerData (struct)


protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// 데이터를 업데이트하는 함수 예시
	// void UpdateRemotePlayerData(uint32 InId, double InX, double InY, double InZ, int32 InInput);
};
