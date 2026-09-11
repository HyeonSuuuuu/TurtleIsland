#include "Network/ChatSubsystem.h"

TArray<FString> UChatSubsystem::GetLines() const
{
	TArray<FString> Lines;
	Lines.Reserve(Messages.Num());
	for (const FChatEntry& Entry : Messages)
	{
		Lines.Add(Entry.Text);
	}
	return Lines;
}

FString UChatSubsystem::GetChatText() const
{
	return FString::Join(GetLines(), TEXT("\n"));
}

int32 UChatSubsystem::AddMessage(const FString& Line, int32 MaxMessages)
{
	FChatEntry Entry;
	Entry.Id = NextMessageId++;
	Entry.Text = Line;
	Entry.AddedAt = FPlatformTime::Seconds();
	Messages.Add(MoveTemp(Entry));

	const int32 Cap = FMath::Max(1, MaxMessages);
	while (Messages.Num() > Cap)
	{
		Messages.RemoveAt(0);
	}

	OnHistoryChanged.Broadcast();
	return Messages.Last().Id;
}

bool UChatSubsystem::PruneExpired(float LifeSeconds)
{
	if (LifeSeconds <= 0.f || Messages.Num() == 0)
	{
		return false;
	}

	const double Now = FPlatformTime::Seconds();
	const int32 Removed = Messages.RemoveAll([Now, LifeSeconds](const FChatEntry& Entry)
	{
		return Now - Entry.AddedAt > LifeSeconds;
	});

	if (Removed > 0)
	{
		OnHistoryChanged.Broadcast();
		return true;
	}

	return false;
}

void UChatSubsystem::ResetHistory()
{
	Messages.Reset();
	NextMessageId = 1;
	OnHistoryChanged.Broadcast();
}
