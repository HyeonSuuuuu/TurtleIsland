#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/ServerAgent.h"
#include "Character/ServerMonster.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/NetMovementInput.h"
#include "Network/ClientPacketCodec.h"
#include "Network/NetSettings.h"
#include "Network/NpcSubsystem.h"
#include "UI/PlayerNameplate.h"
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
			AServerMonster* Monster = Cast<AServerMonster>(Visual);
			if (!TestNotNull(TEXT("Server monster uses dedicated presentation"), Monster)) return false;
			const TCHAR* ExpectedNames[] = {TEXT("\uc2a4\ud1a0\ucee4"), TEXT("\ud0c0\ub974 \ud3ec\ub4dc"), TEXT("\uc13c\ud2b8\ub9ac \uc544\uc774")};
			const TCHAR* ExpectedAttacks[] = {TEXT("Anim_Monster1_Hit_Left_Both"), TEXT("Anim_Monster3_Kick1"), TEXT("Anim_Monster5_HitBoth")};
			TestEqual(TEXT("Type maps to its game display name"), Monster->MonsterDisplayName.ToString(), FString(ExpectedNames[Type]));
			const auto Title = PlayerNameplate::FitText(Monster->MonsterDisplayName.ToString(), PlayerNameplate::NameWidth, 14, 11);
			TestTrue(TEXT("Monster name fits its in-game label"), !Title.Text.IsEmpty() && Title.Size.X <= PlayerNameplate::NameWidth);
			if (!TestNotNull(TEXT("Idle clip"), Monster->IdleAnimation.Get()) ||
				!TestNotNull(TEXT("Movement clip"), Monster->MoveAnimation.Get()) ||
				!TestNotNull(TEXT("Attack clip"), Monster->AttackAnimation.Get()) ||
				!TestNotNull(TEXT("Hit clip"), Monster->HitAnimation.Get()) ||
				!TestNotNull(TEXT("Death clip"), Monster->DeathAnimation.Get())) return false;
			TestEqual(TEXT("One configured basic attack per monster"), Monster->AttackAnimation->GetName(), FString(ExpectedAttacks[Type]));
			Monster->PlayAttack(45.f);
			TestTrue(TEXT("Server attack plays configured clip"), Monster->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() == Monster->AttackAnimation);
			const float ActionTime = Monster->AttackAnimation->GetPlayLength() + 0.1f;
			for (float Elapsed = 0.f; Elapsed < ActionTime; Elapsed += 0.02f)
				TestWorld.TickTestWorld(0.02f);
			TestTrue(TEXT("Basic attack finishes instead of looping"), Monster->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() != Monster->AttackAnimation);
			Monster->FindComponentByClass<UHealthComponent>()->ApplyServerDamage(40, 10, nullptr);
			TestTrue(TEXT("Nonlethal damage plays hit clip"), Monster->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() == Monster->HitAnimation);
			Monster->FindComponentByClass<UHealthComponent>()->ApplyServerDamage(0, 40, nullptr);
			TestNull(TEXT("Dead monster immediately leaves target lookup"), Npcs->FindNpc(Spawn.npcId));
			TestFalse(TEXT("Corpse has no collision"), Monster->GetActorEnableCollision());
			TestTrue(TEXT("Death overrides other animations"), Monster->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() == Monster->DeathAnimation);
			Despawn.npcId = Spawn.npcId;
			Npcs->HandlePacket(EncodeNpcPacket(Despawn, PACKET_ID::SC_NPC_DESPAWN));
			TestFalse(TEXT("Despawn preserves death visual"), Monster->IsActorBeingDestroyed());
			Npcs->HandlePacket(EncodeNpcPacket(Spawn, PACKET_ID::SC_NPC_SPAWN));
			TestTrue(TEXT("Reused ID creates live actor beside old corpse"), Npcs->FindNpc(Spawn.npcId) != Monster);
			const float DeathTime = Monster->DeathAnimation->GetPlayLength() + 0.5f;
			for (float Elapsed = 0.f; Elapsed < DeathTime; Elapsed += 0.02f)
				TestWorld.TickTestWorld(0.02f);
			TestTrue(TEXT("Death visual expires"), !IsValid(Monster) || Monster->IsActorBeingDestroyed());
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
