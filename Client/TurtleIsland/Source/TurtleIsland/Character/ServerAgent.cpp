// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/ServerAgent.h"
#include "Gameplay/NetInputProvider.h"
#include "Gameplay/NetProxyMoverComponent.h"
#include "Gameplay/GroundDetector.h"

AServerAgent::AServerAgent()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;

	// 서버가 조종하는 폰의 컴포넌트 세트
	NetInput = CreateDefaultSubobject<UNetInputProvider>(TEXT("NetInput"));
	Mover    = CreateDefaultSubobject<UNetProxyMoverComponent>(TEXT("Mover"));
	Ground   = CreateDefaultSubobject<UGroundDetector>(TEXT("Ground"));
}

void AServerAgent::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Mover)
	{
		Speed = Mover->GetSpeed2D();
	}
}
