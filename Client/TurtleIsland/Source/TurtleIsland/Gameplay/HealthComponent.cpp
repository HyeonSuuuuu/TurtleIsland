// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/HealthComponent.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetTo(MaxHP, MaxHP);
}

void UHealthComponent::ResetTo(int32 InMaxHP, int32 InHP)
{
	MaxHP     = FMath::Max(1, InMaxHP);
	CurrentHP = FMath::Clamp(InHP, 0, MaxHP);
	bIsDead   = (CurrentHP <= 0);
	OnHealthChanged.Broadcast();
}

void UHealthComponent::ResetToFull()
{
	ResetTo(MaxHP, MaxHP);
}

void UHealthComponent::ResetHP(int32 InHP)
{
	ResetTo(MaxHP, InHP);
}

float UHealthComponent::GetHPRatio() const
{
	return MaxHP > 0 ? FMath::Clamp(static_cast<float>(CurrentHP) / static_cast<float>(MaxHP), 0.f, 1.f) : 0.f;
}

void UHealthComponent::ApplyServerDamage(int32 RemainHP, int32 Damage, AActor* Causer)
{
	if (bIsDead)
	{
		return;
	}

	CurrentHP = FMath::Clamp(RemainHP, 0, MaxHP);
	OnHealthChanged.Broadcast();
	OnDamaged.Broadcast(Damage, GetHPRatio(), Causer);

	if (CurrentHP <= 0)
	{
		ApplyServerDeath(Causer);
	}
}

void UHealthComponent::ApplyServerHeal(int32 RemainHP, int32 Amount)
{
	if (bIsDead)
	{
		return;
	}

	CurrentHP = FMath::Clamp(RemainHP, 0, MaxHP);
	OnHealthChanged.Broadcast();
}

void UHealthComponent::ApplyServerDeath(AActor* Killer)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead   = true;
	CurrentHP = 0;
	OnHealthChanged.Broadcast();
	OnDied.Broadcast(Killer);
}
