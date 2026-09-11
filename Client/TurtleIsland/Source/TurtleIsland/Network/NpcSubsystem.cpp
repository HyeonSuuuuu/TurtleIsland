#include "Network/NpcSubsystem.h"

#include "Character/ServerAgent.h"
#include "Gameplay/GroundDetector.h"
#include "Gameplay/HealthComponent.h"
#include "Network/ClientPacketCodec.h"
#include "Network/NetSettings.h"

namespace
{
	bool IsValidPose(float X, float Y, float Yaw)
	{
		return FMath::IsFinite(X) && FMath::IsFinite(Y) && FMath::IsFinite(Yaw);
	}
}

bool UNpcSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNpcSubsystem::Deinitialize()
{
	SetReady(false);
	Super::Deinitialize();
}

void UNpcSubsystem::SetReady(bool bReady)
{
	bIngameReady = bReady;
	if (!bReady)
	{
		ClearNpcs();
	}
}

void UNpcSubsystem::HandlePacket(const TArray<uint8>& Data)
{
	if (!bIngameReady || Data.Num() < PACKET_HEADER_SIZE)
	{
		return;
	}
	const auto* Header = reinterpret_cast<const PacketHeader*>(Data.GetData());
	if (Header->size != Data.Num())
	{
		return;
	}
	switch (static_cast<PACKET_ID>(Header->id))
	{
	case PACKET_ID::SC_NPC_SPAWN:
		if (Data.Num() == SC_NPC_SPAWN_PACKET_SIZE)
		{
			SpawnNpc(*reinterpret_cast<const SC_NpcSpawn*>(Data.GetData()));
		}
		break;
	case PACKET_ID::SC_NPC_MOVE:
		if (Data.Num() == SC_NPC_MOVE_PACKET_SIZE)
		{
			MoveNpc(*reinterpret_cast<const SC_NpcMove*>(Data.GetData()));
		}
		break;
	case PACKET_ID::SC_NPC_DESPAWN:
		if (Data.Num() == SC_NPC_DESPAWN_PACKET_SIZE)
		{
			DespawnNpc(reinterpret_cast<const SC_NpcDespawn*>(Data.GetData())->npcId);
		}
		break;
	default:
		break;
	}
}

AServerAgent* UNpcSubsystem::FindNpc(uint32 NpcId) const
{
	const TObjectPtr<AServerAgent>* Found = Npcs.Find(NpcId);
	return Found && IsValid(Found->Get()) ? Found->Get() : nullptr;
}

void UNpcSubsystem::SpawnNpc(const SC_NpcSpawn& Packet)
{
	if (Packet.hp <= 0 || Packet.maxHp < Packet.hp || !IsValidPose(Packet.x, Packet.y, Packet.yaw))
	{
		return;
	}
	const TSoftClassPtr<AServerAgent>* ConfiguredClass = UNetSettings::Get()->NpcClasses.Find(Packet.npcType);
	UClass* NpcClass = ConfiguredClass ? ConfiguredClass->LoadSynchronous() : nullptr;
	if (!NpcClass || NpcClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Npc] No spawn class for type %u"), Packet.npcType);
		return;
	}

	AServerAgent* Agent = FindNpc(Packet.npcId);
	if (Agent && (Agent->GetClass() != NpcClass || Agent->FindComponentByClass<UHealthComponent>()->IsDead()))
	{
		DespawnNpc(Packet.npcId);
		Agent = nullptr;
	}
	if (!Agent)
	{
		FTransform Transform(FRotator(0.f, Packet.yaw, 0.f), FVector(Packet.x, Packet.y, 92.f));
		Agent = GetWorld()->SpawnActorDeferred<AServerAgent>(NpcClass, Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Agent)
		{
			return;
		}
		Agent->AutoPossessAI = EAutoPossessAI::Disabled;
		Agent->FindComponentByClass<UHealthComponent>()->MaxHP = Packet.maxHp;
		if (const UGroundDetector* Ground = Agent->FindComponentByClass<UGroundDetector>())
		{
			Transform.SetLocation(FVector(Packet.x, Packet.y, Ground->ResolveActorZ(Packet.x, Packet.y, 92.f)));
		}
		Agent->FinishSpawning(Transform);
		Agent->SetLifeSpan(0.f);
		Npcs.Add(Packet.npcId, Agent);
	}

	UHealthComponent* Health = Agent->FindComponentByClass<UHealthComponent>();
	Health->ResetTo(Packet.maxHp, Packet.hp);
	Agent->UpdateNetState(Packet.x, Packet.y, Packet.yaw);
}

void UNpcSubsystem::MoveNpc(const SC_NpcMove& Packet)
{
	if (IsValidPose(Packet.x, Packet.y, Packet.yaw))
	{
		if (AServerAgent* Agent = FindNpc(Packet.npcId))
		{
			Agent->UpdateNetState(Packet.x, Packet.y, Packet.yaw);
		}
	}
}

void UNpcSubsystem::DespawnNpc(uint32 NpcId)
{
	if (AServerAgent* Agent = FindNpc(NpcId))
	{
		Agent->Destroy();
	}
	Npcs.Remove(NpcId);
}

void UNpcSubsystem::ClearNpcs()
{
	for (const auto& Entry : Npcs)
	{
		if (IsValid(Entry.Value))
		{
			Entry.Value->Destroy();
		}
	}
	Npcs.Reset();
}
