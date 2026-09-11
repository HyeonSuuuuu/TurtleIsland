#pragma once
#include "Network/NetSubsystem.h"
#include "CombatTestFixtures.generated.h"

UCLASS()
class UCombatTestNetSubsystem : public UNetSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return false; }
	virtual UWorld* GetWorld() const override { return TestWorld; }
	void Configure(UWorld* World, uint32 PlayerId) { TestWorld = World; myId = PlayerId; OpponentClass = AServerAgent::StaticClass(); }
	void Receive(const TArray<uint8>& Data) { DispatchPacket(Data); }
	void ResetForTest() { ResetNpcWorld(); }
private:
	UPROPERTY() TObjectPtr<UWorld> TestWorld;
};
