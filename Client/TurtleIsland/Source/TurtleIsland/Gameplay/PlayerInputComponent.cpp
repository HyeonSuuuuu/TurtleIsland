// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/PlayerInputComponent.h"
#include "Gameplay/TurtleInputConfig.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "TurtleIsland.h"
#include "Network/NetSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

UPlayerInputComponent::UPlayerInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPlayerInputComponent::BindInput(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogTurtleIsland, Error, TEXT("%s: Enhanced Input component 를 찾지 못했다."), *GetNameSafe(GetOwner()));
		return;
	}

	if (!InputConfig)
	{
		UE_LOG(LogTurtleIsland, Error, TEXT("%s: InputConfig 가 비어 있어 입력이 하나도 바인딩되지 않는다."), *GetNameSafe(GetOwner()));
		return;
	}

	if (InputConfig->MoveAction)
	{
		EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &UPlayerInputComponent::HandleMove);
		// 손을 떼면 Triggered 가 더 안 오므로 놓임을 따로 알려야 한다
		EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Completed, this, &UPlayerInputComponent::HandleMoveEnd);
		EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Canceled,  this, &UPlayerInputComponent::HandleMoveEnd);
	}

	if (InputConfig->LookAction)      { EIC->BindAction(InputConfig->LookAction,      ETriggerEvent::Triggered, this, &UPlayerInputComponent::HandleLook); }
	if (InputConfig->MouseLookAction) { EIC->BindAction(InputConfig->MouseLookAction, ETriggerEvent::Triggered, this, &UPlayerInputComponent::HandleLook); }

	if (InputConfig->JumpAction)
	{
		EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Started,   this, &UPlayerInputComponent::HandleJumpStart);
		EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Completed, this, &UPlayerInputComponent::HandleJumpEnd);
	}

	if (InputConfig->InteractAction)
	{
		EIC->BindAction(InputConfig->InteractAction, ETriggerEvent::Started, this, &UPlayerInputComponent::HandleInteract);
	}

	const FKey ItemKeys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};
	for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(ItemKeys); ++Slot)
	{
		FInputKeyBinding Binding(FInputChord(ItemKeys[Slot]), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &UPlayerInputComponent::HandleItemSlot, Slot);
		EIC->KeyBindings.Add(MoveTemp(Binding));
	}

	// 스킬은 개별 함수를 만들지 않는다. ID 를 페이로드로 실어 하나로 받는다.
	for (const FSkillInputAction& Entry : InputConfig->SkillActions)
	{
		if (Entry.Action)
		{
			EIC->BindAction(Entry.Action, ETriggerEvent::Started, this, &UPlayerInputComponent::HandleSkill, Entry.SkillId);
		}
	}
}

void UPlayerInputComponent::HandleMove(const FInputActionValue& Value)      { OnMoveAxis.Broadcast(Value.Get<FVector2D>()); }
void UPlayerInputComponent::HandleMoveEnd(const FInputActionValue& Value)   { OnMoveReleased.Broadcast(); }
void UPlayerInputComponent::HandleLook(const FInputActionValue& Value)      { OnLookAxis.Broadcast(Value.Get<FVector2D>()); }
void UPlayerInputComponent::HandleJumpStart(const FInputActionValue& Value) { OnJumpPressed.Broadcast(); }
void UPlayerInputComponent::HandleJumpEnd(const FInputActionValue& Value)   { OnJumpReleased.Broadcast(); }
void UPlayerInputComponent::HandleInteract(const FInputActionValue& Value)  { OnInteractPressed.Broadcast(); }

void UPlayerInputComponent::HandleSkill(const FInputActionValue& Value, int32 SkillId)
{
	OnSkillPressed.Broadcast(SkillId);
}

void UPlayerInputComponent::HandleItemSlot(int32 Slot)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || !Pawn->GetController() || Pawn->GetController()->IsMoveInputIgnored()) return;
	if (UGameInstance* GI = GetWorld()->GetGameInstance())
		if (UNetSubsystem* Net = GI->GetSubsystem<UNetSubsystem>()) Net->UseItem(Slot);
}
