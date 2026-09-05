// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/PlayerStatsComponent.h"

UPlayerStatsComponent::UPlayerStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPlayerStatsComponent::BeginPlay()
{
	Super::BeginPlay();

	Gold = StartingGold;
	RecalcExpToNext();
	OnStatsChanged.Broadcast();
}

void UPlayerStatsComponent::RecalcExpToNext()
{
	ExpToNext = FMath::Max(1, BaseExpToNext + ExpGrowthPerLevel * (Level - 1));
}

void UPlayerStatsComponent::AddExp(int32 Amount)
{
	if (Amount <= 0 || IsMaxLevel())
	{
		return;
	}

	Exp += Amount;

	while (Exp >= ExpToNext && !IsMaxLevel())
	{
		Exp -= ExpToNext;
		++Level;
		RecalcExpToNext();
		OnLevelUp.Broadcast(Level);
	}

	// 만렙에 닿으면 남은 경험치는 버리고 바를 가득 채운 상태로 둔다
	if (IsMaxLevel())
	{
		Exp = ExpToNext;
	}

	OnStatsChanged.Broadcast();
}

void UPlayerStatsComponent::AddGold(int32 Amount)
{
	if (Amount == 0)
	{
		return;
	}

	Gold = FMath::Max(0, Gold + Amount);
	OnStatsChanged.Broadcast();
}

bool UPlayerStatsComponent::SpendGold(int32 Amount)
{
	if (Amount <= 0 || Gold < Amount)
	{
		return false;
	}

	Gold -= Amount;
	OnStatsChanged.Broadcast();
	return true;
}

float UPlayerStatsComponent::GetExpRatio() const
{
	return ExpToNext > 0 ? FMath::Clamp(static_cast<float>(Exp) / static_cast<float>(ExpToNext), 0.f, 1.f) : 0.f;
}

void UPlayerStatsComponent::ApplyServerStats(int32 InLevel, int32 InExp, int32 InExpToNext, int32 InGold)
{
	const int32 PrevLevel = Level;

	Level     = FMath::Max(1, InLevel);
	Exp       = FMath::Max(0, InExp);
	ExpToNext = FMath::Max(1, InExpToNext);
	Gold      = FMath::Max(0, InGold);

	if (Level > PrevLevel)
	{
		OnLevelUp.Broadcast(Level);
	}
	OnStatsChanged.Broadcast();
}
