#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/ServerAgent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/NetMovementInput.h"
#include "Network/ClientPacketCodec.h"
#include "Network/NetSettings.h"
#include "Network/NpcSubsystem.h"
#include <limits>

namespace
{
	template<typename T>
	TArray<uint8> EncodeNpcPacket(T Packet, PACKET_ID Id)
	{
		Packet.size = sizeof(T);
		Packet.id = static_cast<uint16>(Id);
		TArray<uint8> Data;
		Data.Append(reinterpret_cast<const uint8*>(&Packet), sizeof(T));
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcLifecycleTest,
	"TurtleIsland.Network.Npc.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNpcLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	UNpcSubsystem* Npcs = World->GetSubsystem<UNpcSubsystem>();
	if (!TestNotNull(TEXT("Game world owns NPC registry"), Npcs)) return false;

	SC_NpcSpawn Spawn{};
	Spawn.npcId = 0;
	Spawn.npcType = 2;
	Spawn.hp = 150;
	Spawn.maxHp = 150;
	Spawn.x = 100.f;
	Spawn.y = 200.f;
	Spawn.yaw = 90.f;
	const auto SpawnData = EncodeNpcPacket(Spawn, PACKET_ID::SC_NPC_SPAWN);
	Npcs->HandlePacket(SpawnData);
	TestNull(TEXT("Pre-ready snapshot cannot spawn in old map"), Npcs->FindNpc(0));
	Npcs->SetReady(true);
	Npcs->HandlePacket(SpawnData);
	AServerAgent* First = Npcs->FindNpc(0);
	if (!TestNotNull(TEXT("Spawn creates configured monster"), First)) return false;
	TestTrue(TEXT("Spawn completed BeginPlay"), First->HasActorBegunPlay());
	TestEqual(TEXT("Server HP over 100 is preserved"), First->FindComponentByClass<UHealthComponent>()->CurrentHP, 150);
	TestNull(TEXT("No local AI controller"), First->GetController());
	TestNotNull(TEXT("Monster mesh loaded"), First->GetMesh()->GetSkeletalMeshAsset());
	TestNotNull(TEXT("Monster animation loaded"), First->GetMesh()->GetAnimInstance());
	Npcs->HandlePacket(SpawnData);
	TestTrue(TEXT("Duplicate spawn reuses actor"), First == Npcs->FindNpc(0));
	Spawn.hp = 120;
	Npcs->HandlePacket(EncodeNpcPacket(Spawn, PACKET_ID::SC_NPC_SPAWN));
	TestEqual(TEXT("Snapshot updates HP"), First->FindComponentByClass<UHealthComponent>()->CurrentHP, 120);
	TestEqual(TEXT("Snapshot preserves known maximum"), First->FindComponentByClass<UHealthComponent>()->MaxHP, 150);

	SC_NpcMove Move{};
	Move.npcId = 0;
	Move.x = 300.f;
	Move.y = 400.f;
	Move.yaw = 180.f;
	Npcs->HandlePacket(EncodeNpcPacket(Move, PACKET_ID::SC_NPC_MOVE));
	TestWorld.TickTestWorld();
	TestEqual(TEXT("Move reaches proxy target"), First->FindComponentByClass<UNetMovementInput>()->GetTargetLocation().X, 300.0);
	TestTrue(TEXT("Proxy moves using server state"), First->GetActorLocation().X > 100.f);
	Move.x = std::numeric_limits<float>::quiet_NaN();
	Npcs->HandlePacket(EncodeNpcPacket(Move, PACKET_ID::SC_NPC_MOVE));
	TestEqual(TEXT("Non-finite move is ignored"), First->FindComponentByClass<UNetMovementInput>()->GetTargetLocation().X, 300.0);

	SC_NpcDespawn Despawn{};
	Despawn.npcId = 0;
	auto BadDespawn = EncodeNpcPacket(Despawn, PACKET_ID::SC_NPC_DESPAWN);
	BadDespawn.Add(0);
	reinterpret_cast<PacketHeader*>(BadDespawn.GetData())->size = BadDespawn.Num();
	Npcs->HandlePacket(BadDespawn);
	TestTrue(TEXT("Oversized despawn is rejected"), First == Npcs->FindNpc(0));
	const auto DespawnData = EncodeNpcPacket(Despawn, PACKET_ID::SC_NPC_DESPAWN);
	Npcs->HandlePacket(DespawnData);
	TestNull(TEXT("Despawn removes registry entry"), Npcs->FindNpc(0));
	TestTrue(TEXT("Despawn destroys actor"), First->IsActorBeingDestroyed());
	Npcs->HandlePacket(DespawnData);
	Move.x = 500.f;
	Npcs->HandlePacket(EncodeNpcPacket(Move, PACKET_ID::SC_NPC_MOVE));
	TestNull(TEXT("Late move does not recreate despawned NPC"), Npcs->FindNpc(0));
	auto TruncatedSpawn = SpawnData;
	TruncatedSpawn.Pop();
	reinterpret_cast<PacketHeader*>(TruncatedSpawn.GetData())->size = TruncatedSpawn.Num();
	Npcs->HandlePacket(TruncatedSpawn);
	TestNull(TEXT("Truncated spawn is rejected"), Npcs->FindNpc(0));
	Spawn.hp = 0;
	Npcs->HandlePacket(EncodeNpcPacket(Spawn, PACKET_ID::SC_NPC_SPAWN));
	TestNull(TEXT("Dead spawn is rejected"), Npcs->FindNpc(0));
	Npcs->HandlePacket(SpawnData);
	AServerAgent* Respawned = Npcs->FindNpc(0);
	TestTrue(TEXT("Explicit respawn creates new actor"), Respawned && Respawned != First);

	for (uint16 Type = 0; Type < 3; ++Type)
	{
		Spawn.npcId = Type + 10;
		Spawn.npcType = Type;
		Spawn.hp = 50;
		Spawn.maxHp = 50;
		Npcs->HandlePacket(EncodeNpcPacket(Spawn, PACKET_ID::SC_NPC_SPAWN));
		AServerAgent* Visual = Npcs->FindNpc(Spawn.npcId);
		if (TestNotNull(TEXT("Configured NPC type spawns"), Visual))
		{
			TestNotNull(TEXT("Configured visual has skeletal mesh"), Visual->GetMesh()->GetSkeletalMeshAsset());
			TestNotNull(TEXT("Configured visual has animation instance"), Visual->GetMesh()->GetAnimInstance());
		}
	}
	Npcs->SetReady(false);
	TestNull(TEXT("Session end clears monsters"), Npcs->FindNpc(0));
	TestTrue(TEXT("Session end destroys monster actors"), Respawned && Respawned->IsActorBeingDestroyed());
	Npcs->HandlePacket(SpawnData);
	TestNull(TEXT("Late spawn after leave is ignored"), Npcs->FindNpc(0));
	Npcs->SetReady(true);
	Npcs->HandlePacket(SpawnData);
	TWeakObjectPtr<AServerAgent> BeforeTravel = Npcs->FindNpc(0);
	TestWorld.DestroyTestWorld(false);
	TestFalse(TEXT("World teardown releases monster"), BeforeTravel.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
