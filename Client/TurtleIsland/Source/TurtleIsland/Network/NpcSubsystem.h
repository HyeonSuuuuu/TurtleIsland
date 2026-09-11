#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NpcSubsystem.generated.h"

class AServerAgent;
struct SC_NpcSpawn;
struct SC_NpcMove;

// NPC ids are room-local and must not share the player lookup table.
UCLASS()
class TURTLEISLAND_API UNpcSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	void SetReady(bool bReady);
	void HandlePacket(const TArray<uint8>& Data);
	AServerAgent* FindNpc(uint32 NpcId) const;
	void ClearNpcs();

private:
	void SpawnNpc(const SC_NpcSpawn& Packet);
	void MoveNpc(const SC_NpcMove& Packet);
	void DespawnNpc(uint32 NpcId);

	bool bIngameReady = false;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<AServerAgent>> Npcs;
};
