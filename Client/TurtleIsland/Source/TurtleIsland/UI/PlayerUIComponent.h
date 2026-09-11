#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerUIComponent.generated.h"

class UUserWidget;

UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class TURTLEISLAND_API UPlayerUIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerUIComponent();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void BeginUIInteraction(UUserWidget* OwnerWidget, FName Context);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void EndUIInteraction(UUserWidget* OwnerWidget);

	UFUNCTION(BlueprintPure, Category = "UI")
	bool HasActiveUIInteraction() const { return ActiveInteractions.Num() > 0; }

private:
	struct FUIInteraction
	{
		TWeakObjectPtr<UUserWidget> OwnerWidget;
		FName Context;
		bool bBlockedInput = false;
	};

	void ApplyTopInteraction();
	void ReturnToGameInputIfIdle();
	APlayerController* GetOwningPlayerController() const;

	TArray<FUIInteraction> ActiveInteractions;
};
