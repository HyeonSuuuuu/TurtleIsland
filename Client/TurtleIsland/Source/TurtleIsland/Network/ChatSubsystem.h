#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChatSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FChatEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FString Text;

	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	double AddedAt = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChatHistoryChanged);

UCLASS()
class TURTLEISLAND_API UChatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	const TArray<FChatEntry>& GetMessages() const { return Messages; }

	UFUNCTION(BlueprintPure, Category = "Chat")
	TArray<FString> GetLines() const;

	UFUNCTION(BlueprintPure, Category = "Chat")
	FString GetChatText() const;

	int32 AddMessage(const FString& Line, int32 MaxMessages);
	bool PruneExpired(float LifeSeconds);
	void ResetHistory();

	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FOnChatHistoryChanged OnHistoryChanged;

private:
	UPROPERTY()
	TArray<FChatEntry> Messages;

	int32 NextMessageId = 1;
};
