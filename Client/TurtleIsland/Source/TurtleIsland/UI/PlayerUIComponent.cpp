#include "UI/PlayerUIComponent.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

UPlayerUIComponent::UPlayerUIComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPlayerUIComponent::BeginUIInteraction(UUserWidget* OwnerWidget, FName Context)
{
	if (!OwnerWidget)
	{
		return;
	}

	EndUIInteraction(OwnerWidget);

	FUIInteraction Interaction;
	Interaction.OwnerWidget = OwnerWidget;
	Interaction.Context = Context;
	Interaction.bBlockedInput = true;
	ActiveInteractions.Add(Interaction);

	if (APlayerController* PC = GetOwningPlayerController())
	{
		PC->SetIgnoreLookInput(true);
		PC->SetIgnoreMoveInput(true);
	}

	ApplyTopInteraction();
}

void UPlayerUIComponent::EndUIInteraction(UUserWidget* OwnerWidget)
{
	if (!OwnerWidget)
	{
		return;
	}

	for (int32 Index = ActiveInteractions.Num() - 1; Index >= 0; --Index)
	{
		const bool bMatches = !ActiveInteractions[Index].OwnerWidget.IsValid()
			|| ActiveInteractions[Index].OwnerWidget.Get() == OwnerWidget;
		if (!bMatches)
		{
			continue;
		}

		if (ActiveInteractions[Index].bBlockedInput)
		{
			if (APlayerController* PC = GetOwningPlayerController())
			{
				PC->SetIgnoreLookInput(false);
				PC->SetIgnoreMoveInput(false);
			}
		}
		ActiveInteractions.RemoveAt(Index);
	}

	ApplyTopInteraction();
	ReturnToGameInputIfIdle();
}

void UPlayerUIComponent::ApplyTopInteraction()
{
	ActiveInteractions.RemoveAll([](const FUIInteraction& Interaction)
	{
		return !Interaction.OwnerWidget.IsValid();
	});

	if (ActiveInteractions.Num() == 0)
	{
		return;
	}

	const FUIInteraction& Top = ActiveInteractions.Last();
	if (APlayerController* PC = GetOwningPlayerController())
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Top.OwnerWidget->TakeWidget());
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
}

void UPlayerUIComponent::ReturnToGameInputIfIdle()
{
	if (ActiveInteractions.Num() > 0)
	{
		return;
	}

	if (APlayerController* PC = GetOwningPlayerController())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}

APlayerController* UPlayerUIComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}
