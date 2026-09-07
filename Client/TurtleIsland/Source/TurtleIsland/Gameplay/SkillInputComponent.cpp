// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/SkillInputComponent.h"
#include "Network/NetSubsystem.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

USkillInputComponent::USkillInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USkillInputComponent::BindInput(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		return;
	}

	if (AttackAction) { EIC->BindAction(AttackAction, ETriggerEvent::Started, this, &USkillInputComponent::HandleAttack); }
	if (SkillQAction) { EIC->BindAction(SkillQAction, ETriggerEvent::Started, this, &USkillInputComponent::HandleSkillQ); }
	if (SkillEAction) { EIC->BindAction(SkillEAction, ETriggerEvent::Started, this, &USkillInputComponent::HandleSkillE); }
}

void USkillInputComponent::HandleAttack(const FInputActionValue& Value) { RequestSkill(AttackSkillId); }
void USkillInputComponent::HandleSkillQ(const FInputActionValue& Value) { RequestSkill(SkillQId); }
void USkillInputComponent::HandleSkillE(const FInputActionValue& Value) { RequestSkill(SkillEId); }

void USkillInputComponent::RequestSkill(int32 SkillId)
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UNetSubsystem* Net = GI ? GI->GetSubsystem<UNetSubsystem>() : nullptr;
	if (!Owner || !Net)
	{
		return;
	}

	Net->SendAttack(SkillId, CurrentTargetId, Owner->GetActorRotation().Yaw);
}
