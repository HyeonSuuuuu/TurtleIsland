#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/CombatTestFixtures.h"
#include "Character/PlayerAgent.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/LocalMoverComponent.h"
#include "Gameplay/AgentSkillComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/UserInterfaceSettings.h"
#include "Gameplay/NetMovementInput.h"
#include "Gameplay/PlayerStatsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Network/NpcSubsystem.h"
#include "GameplayRules.h"
#include "UI/MainHUDWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/UniformGridPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTopDownCameraDefaultsTest, "TurtleIsland.Gameplay.TopDownCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTopDownCameraDefaultsTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	UClass* PlayerClass = LoadClass<APlayerAgent>(nullptr, TEXT("/Game/Lv3_Ingame/Paladin/BP_Paladin.BP_Paladin_C"));
	if (!TestNotNull(TEXT("Player blueprint"), PlayerClass)) return false;
	APlayerAgent* Player = World->SpawnActor<APlayerAgent>(PlayerClass);
	if (!TestNotNull(TEXT("Player created"), Player)) return false;
	const USpringArmComponent* Boom = Player->FindComponentByClass<USpringArmComponent>();
	const UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>();
	if (!TestNotNull(TEXT("Camera boom"), Boom) || !TestNotNull(TEXT("Camera"), Camera)) return false;
	TestEqual(TEXT("Top-down distance"), Boom->TargetArmLength, 1400.f);
	TestEqual(TEXT("Downward pitch"), Boom->GetTargetRotation().Pitch, -55.0);
	TestEqual(TEXT("Movement uses camera yaw"), Player->GetCameraViewYaw(), 45.f);
	TestTrue(TEXT("Character turns do not rotate camera"), Boom->IsUsingAbsoluteRotation());
	TestFalse(TEXT("Mouse look cannot tilt camera"), Boom->bUsePawnControlRotation);
	TestEqual(TEXT("Field of view"), Camera->FieldOfView, 55.f);
	Player->SetActorRotation(FRotator(10.f, 180.f, 15.f));
	TestEqual(TEXT("Character rotation keeps camera pitch"), Boom->GetTargetRotation().Pitch, -55.0);
	TestEqual(TEXT("Character rotation keeps movement basis"), Player->GetCameraViewYaw(), 45.f);
	Wrapper.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FViewportUIScalingTest, "TurtleIsland.Gameplay.ViewportUIScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FViewportUIScalingTest::RunTest(const FString& Parameters)
{
	const UUserInterfaceSettings* Settings = GetDefault<UUserInterfaceSettings>();
	TestEqual(TEXT("FHD design scale"), Settings->GetDPIScaleBasedOnSize(FIntPoint(1920, 1080)), 1.f);
	TestEqual(TEXT("QHD scale"), Settings->GetDPIScaleBasedOnSize(FIntPoint(2560, 1440)), 4.f / 3.f);
	TestEqual(TEXT("4K scale"), Settings->GetDPIScaleBasedOnSize(FIntPoint(3840, 2160)), 2.f);
	TestEqual(TEXT("Ultrawide keeps vertical scale"), Settings->GetDPIScaleBasedOnSize(FIntPoint(3440, 1440)), 4.f / 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAuthorityTest, "TurtleIsland.Gameplay.AnimationAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAuthorityTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	UClass* PlayerClass = LoadClass<APlayerAgent>(nullptr, TEXT("/Game/Lv3_Ingame/Paladin/BP_Paladin.BP_Paladin_C"));
	if (!TestNotNull(TEXT("Player blueprint"), PlayerClass)) return false;
	APlayerAgent* Agent = World->SpawnActor<APlayerAgent>(PlayerClass);
	if (!TestNotNull(TEXT("Agent"), Agent)) return false;
	UAgentSkillComponent* Skills = Agent->FindComponentByClass<UAgentSkillComponent>();
	UAnimInstance* Anim = Agent->GetMesh()->GetAnimInstance();
	if (!TestNotNull(TEXT("Skill component"), Skills) || !TestNotNull(TEXT("Animation instance"), Anim)) return false;
	UCharacterMovementComponent* Move = Agent->GetCharacterMovement();
	Move->bRunPhysicsWithNoController = true;
	Move->bOrientRotationToMovement = false;
	Move->SetMovementMode(MOVE_Flying);
	Agent->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	for (int32 SkillId : {0, 2})
	{
		UAnimMontage* Montage = Skills->SkillMontages.FindRef(SkillId);
		if (!TestNotNull(TEXT("Root-motion attack montage"), Montage)) return false;
		Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		Agent->SetActorRotation(FRotator(12.f, 31.f, -8.f));
		Skills->PlaySkill(SkillId, 90.f);
		TestTrue(TEXT("Attack montage is playing"), Anim->Montage_IsPlaying(Montage));
		TestTrue(TEXT("Animation cannot drive actor transform"), Anim->RootMotionMode == ERootMotionMode::IgnoreRootMotion);
		TestEqual(TEXT("Attack clears pitch tilt"), Agent->GetActorRotation().Pitch, 0.0);
		TestEqual(TEXT("Attack clears roll tilt"), Agent->GetActorRotation().Roll, 0.0);
		TestEqual(TEXT("Attack keeps server yaw"), Agent->GetActorRotation().Yaw, 90.0);
		const FVector StartLocation = Agent->GetActorLocation();
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Wrapper.TickTestWorld();
			TestTrue(TEXT("Animation playback keeps actor upright"), Agent->GetActorRotation().Equals(FRotator(0.f, 90.f, 0.f)));
			TestTrue(TEXT("Animation playback cannot displace actor"), Agent->GetActorLocation().Equals(StartLocation));
		}
	}
	Wrapper.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingRecoveryTest, "TurtleIsland.Gameplay.LandingRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandingRecoveryTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	ACharacter* Character = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Character created"), Character)) return false;
	ULocalMoverComponent* Mover = NewObject<ULocalMoverComponent>(Character);
	Character->AddInstanceComponent(Mover);
	Mover->RegisterComponent();
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	Move->SetComponentTickEnabled(false);
	TestFalse(TEXT("No initial landing lock"), Mover->IsLandingLocked());
	Move->Velocity = FVector(500, 0, -100);
	Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
	Character->Jump();
	Character->LandedDelegate.Broadcast(FHitResult());
	TestTrue(TEXT("Landing locks even without animation assets"), Mover->IsLandingLocked());
	TestTrue(TEXT("Landing stops drift immediately"), Move->Velocity.IsNearlyZero());
	TestTrue(TEXT("Landing clears buffered movement"), Character->GetPendingMovementInputVector().IsNearlyZero());
	TestFalse(TEXT("Landing cancels held jump"), Character->bPressedJump);
	Move->Velocity = FVector(500, 0, -25);
	Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
	Mover->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Recovery stops horizontal velocity"), Move->Velocity.X, 0.0);
	TestEqual(TEXT("Recovery preserves gravity"), Move->Velocity.Z, -25.0);
	TestTrue(TEXT("Recovery consumes buffered input"), Character->GetPendingMovementInputVector().IsNearlyZero());
	// Advance normal frames; a single long frame can be clamped by world settings.
	const int32 RecoveryFrames = GameplayRules::LandingRecoveryMs / 10;
	for (int32 Frame = 0; Frame < RecoveryFrames - 1; ++Frame) Wrapper.TickTestWorld(0.01f);
	TestTrue(TEXT("Recovery remains active before deadline"), Mover->IsLandingLocked());
	Wrapper.TickTestWorld(0.01f);
	Wrapper.TickTestWorld(0.01f);
	TestFalse(TEXT("Recovery expires"), Mover->IsLandingLocked());
	Wrapper.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FServerGameplayRulesTest, "TurtleIsland.Gameplay.ServerRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FServerGameplayRulesTest::RunTest(const FString& Parameters)
{
	GameplayRules::PlayerState State;
	TestTrue(TEXT("Unready cannot cast"), GameplayRules::UseSkill(State, 1, 100) == ActionResult::Invalid);
	State.ready = true;
	TestTrue(TEXT("Skill accepted"), GameplayRules::UseSkill(State, 1, 100) == ActionResult::Ok);
	TestEqual(TEXT("Mana spent once"), State.mp, 80);
	TestTrue(TEXT("Cooldown rejects duplicate"), GameplayRules::UseSkill(State, 1, 101) == ActionResult::Cooldown);
	TestEqual(TEXT("Rejected cast keeps mana"), State.mp, 80);
	State.mp = 0;
	TestTrue(TEXT("Insufficient mana"), GameplayRules::UseSkill(State, 2, 5000) == ActionResult::NotEnoughMP);
	TestTrue(TEXT("Unknown skill"), GameplayRules::UseSkill(State, 3, 5000) == ActionResult::Invalid);
	CS_UseItem Item{}; Item.slot = 1; Item.expectedItemId = 2; Item.inventoryRevision = State.inventoryRevision;
	TestTrue(TEXT("Mana potion accepted"), GameplayRules::UseItem(State, Item, 5000) == ActionResult::Ok);
	TestEqual(TEXT("Mana restored"), State.mp, 50);
	TestEqual(TEXT("Stack consumed"), State.items[1].count, static_cast<uint16>(2));
	TestTrue(TEXT("Duplicate inventory request rejected"), GameplayRules::UseItem(State, Item, 7000) == ActionResult::StaleInventory);
	Item.inventoryRevision = State.inventoryRevision;
	TestTrue(TEXT("Shared item cooldown"), GameplayRules::UseItem(State, Item, 5001) == ActionResult::Cooldown);
	Item.slot = 6;
	TestTrue(TEXT("Slot bounds enforced"), GameplayRules::UseItem(State, Item, 7000) == ActionResult::Invalid);
	Item.slot = 0; Item.expectedItemId = 1;
	TestTrue(TEXT("Full HP does not consume potion"), GameplayRules::UseItem(State, Item, 7000) == ActionResult::NoEffect);
	State.hp = 90;
	TestTrue(TEXT("Health potion accepted"), GameplayRules::UseItem(State, Item, 7000) == ActionResult::Ok);
	TestEqual(TEXT("Potion clamped to maximum"), State.hp, 100);
	State.hp = 0;
	Item.inventoryRevision = State.inventoryRevision;
	TestTrue(TEXT("Dead cannot use items"), GameplayRules::UseItem(State, Item, 9000) == ActionResult::Dead);
	TestTrue(TEXT("Dead cannot attack"), GameplayRules::UseSkill(State, 0, 9000) == ActionResult::Dead);
	for (int i = 0; i < 4; ++i) GameplayRules::RewardKill(State);
	TestEqual(TEXT("Server awards gold"), State.gold, 40);
	TestEqual(TEXT("Server levels player"), State.level, 2);
	TestEqual(TEXT("Level-up exp carry"), State.exp, 0);
	return true;
}

namespace
{
	template<typename T> TArray<uint8> PacketBytes(T Packet, PACKET_ID Id)
	{
		Packet.size = sizeof(Packet); Packet.id = static_cast<uint16>(Id);
		TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(&Packet), sizeof(Packet)); return Bytes;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatNetworkTest, "TurtleIsland.Network.CombatState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatNetworkTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	UCombatTestNetSubsystem* Net = NewObject<UCombatTestNetSubsystem>(World->GetGameInstance());
	Net->Configure(World, 42);
	World->GetSubsystem<UNpcSubsystem>()->SetReady(true);
	SC_MoveNotify Move{}; Move.userIndex = 0; Move.x = 100; Move.z = 250; Move.velocityZ = 200; Move.airborne = 1;
	Move.hp = 100; Move.maxHP = 100;
	Net->Receive(PacketBytes(Move, PACKET_ID::SC_MOVE_NOTIFY));
	AServerAgent* Remote = Cast<AServerAgent>(Net->FindEntity(ENTITY_PLAYER, 0));
	if (!TestNotNull(TEXT("Remote player created"), Remote)) return false;
	Wrapper.TickTestWorld();
	TestTrue(TEXT("Remote animation sees falling"), Remote->GetCharacterMovement()->IsFalling());
	TestFalse(TEXT("Remote physics disabled"), Remote->GetCharacterMovement()->IsComponentTickEnabled());
	TestEqual(TEXT("Remote Z not replaced with ground"), Remote->GetActorLocation().Z, 250.0);
	TestEqual(TEXT("Vertical velocity synchronized"), Remote->GetVelocity().Z, 200.0);
	Move.airborne = 0; Move.velocityZ = 0; Move.z = 96;
	Net->Receive(PacketBytes(Move, PACKET_ID::SC_MOVE_NOTIFY));
	Wrapper.TickTestWorld();
	TestFalse(TEXT("Landing clears falling"), Remote->GetCharacterMovement()->IsFalling());
	SC_NpcSpawn Spawn{}; Spawn.npcId = 0; Spawn.npcType = 0; Spawn.hp = 150; Spawn.maxHp = 150;
	Net->Receive(PacketBytes(Spawn, PACKET_ID::SC_NPC_SPAWN));
	ATurtleAgent* Npc = Net->FindEntity(ENTITY_NPC, 0);
	if (!TestNotNull(TEXT("NPC uses separate namespace"), Npc)) return false;
	SC_DamageNotify Damage{}; Damage.targetId = 0; Damage.targetKind = ENTITY_NPC;
	Damage.causerId = 42; Damage.causerKind = ENTITY_PLAYER; Damage.damage = 40; Damage.remainHP = 110; Damage.maxHP = 150;
	Net->Receive(PacketBytes(Damage, PACKET_ID::SC_DAMAGE_NOTIFY));
	TestEqual(TEXT("Server HP applied to NPC"), Npc->FindComponentByClass<UHealthComponent>()->CurrentHP, 110);
	TestEqual(TEXT("Overlapping player ID unaffected"), Remote->FindComponentByClass<UHealthComponent>()->CurrentHP, 100);
	SC_HealthState Health{}; Health.playerId = 0; Health.hp = 70; Health.maxHP = 110;
	Net->Receive(PacketBytes(Health, PACKET_ID::SC_HEALTH_STATE));
	TestEqual(TEXT("Remote health snapshot"), Remote->FindComponentByClass<UHealthComponent>()->CurrentHP, 70);
	Health.hp = 110;
	Net->Receive(PacketBytes(Health, PACKET_ID::SC_HEALTH_STATE));
	TestEqual(TEXT("Remote potion heal synchronized"), Remote->FindComponentByClass<UHealthComponent>()->CurrentHP, 110);
	SC_PlayerState State{}; State.playerId = 42; State.hp = 75; State.maxHP = 100; State.mp = 60; State.maxMP = 100;
	State.level = 2; State.exp = 10; State.expToNext = 150; State.gold = 120; State.moveSpeed = 500;
	State.items[0] = {1, 2}; State.inventoryRevision = 3;
	Net->Receive(PacketBytes(State, PACKET_ID::SC_PLAYER_STATE));
	if (!TestNotNull(TEXT("Owner state cached for HUD"), Net->GetPlayerState())) return false;
	TestEqual(TEXT("HUD gold"), Net->GetPlayerState()->gold, 120);
	TestEqual(TEXT("HUD inventory"), Net->GetPlayerState()->items[0].count, static_cast<uint16>(2));
	UClass* HUDClass = LoadClass<UMainHUDWidget>(nullptr, TEXT("/Game/Lv3_Ingame/MainUI/WBP_MainHUD.WBP_MainHUD_C"));
	if (!TestNotNull(TEXT("HUD blueprint inherits native binding"), HUDClass)) return false;
	UMainHUDWidget* HUD = CreateWidget<UMainHUDWidget>(World->GetGameInstance(), HUDClass);
	if (!TestNotNull(TEXT("HUD created"), HUD)) return false;
	const TSharedRef<SWidget> SlateHUD = HUD->TakeWidget();
	HUD->BindNetwork(Net);
	UTextBlock* Gold = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("GoldText")));
	UProgressBar* HP = Cast<UProgressBar>(HUD->GetWidgetFromName(TEXT("HPProgressBar")));
	UUniformGridPanel* Inventory = Cast<UUniformGridPanel>(HUD->GetWidgetFromName(TEXT("InventoryGrid")));
	if (!TestNotNull(TEXT("Gold field created"), Gold) || !TestNotNull(TEXT("HP bar bound"), HP) ||
		!TestNotNull(TEXT("Inventory bound"), Inventory)) return false;
	TestEqual(TEXT("HUD renders cached gold"), Gold->GetText().ToString(), FString(TEXT("  120 G")));
	TestEqual(TEXT("HUD renders HP ratio"), HP->GetPercent(), 0.75f);
	TestEqual(TEXT("Exactly six inventory slots"), Inventory->GetChildrenCount(), 6);
	for (UWidget* Child : Inventory->GetAllChildren()) TestTrue(TEXT("Inventory slot is clickable"), Child->IsA<UQuickSlotButton>());
	State.hp = 50; State.gold = 130;
	Net->Receive(PacketBytes(State, PACKET_ID::SC_PLAYER_STATE));
	TestEqual(TEXT("HUD delegate updates HP"), HP->GetPercent(), 0.5f);
	TestEqual(TEXT("HUD delegate updates gold"), Gold->GetText().ToString(), FString(TEXT("  130 G")));
	APlayerController* PC = World->SpawnActor<APlayerController>();
	FActorSpawnParameters LocalParams;
	LocalParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATurtleAgent* Local = World->SpawnActor<AServerAgent>(AServerAgent::StaticClass(), FVector(900, 900, 96), FRotator::ZeroRotator, LocalParams);
	if (!TestNotNull(TEXT("Local controller created"), PC) || !TestNotNull(TEXT("Local agent created"), Local)) return false;
	PC->Possess(Local);
	Local->SetActorLocation(FVector(900, 900, 96));
	Local->GetCharacterMovement()->Velocity = FVector(500, 0, 0);
	SC_MoveNotify Correction{};
	Correction.userIndex = 42; Correction.x = 100; Correction.y = 200; Correction.z = 96;
	Correction.hp = 50; Correction.maxHP = 100;
	Net->Receive(PacketBytes(Correction, PACKET_ID::SC_MOVE_NOTIFY));
	TestTrue(TEXT("Own correction restores authoritative position"), Local->GetActorLocation().Equals(FVector(100, 200, 96)));
	TestTrue(TEXT("Own correction stops rejected movement"), Local->GetVelocity().IsNearlyZero());
	Correction.airborne = 1; Correction.z = 196; Correction.velocityZ = 300;
	Net->Receive(PacketBytes(Correction, PACKET_ID::SC_MOVE_NOTIFY));
	TestTrue(TEXT("Own correction retains airborne state"), Local->GetCharacterMovement()->IsFalling());
	TestEqual(TEXT("Own correction restores jump velocity"), Local->GetVelocity().Z, 300.0);
	State.playerId = 99; State.gold = 999;
	Net->Receive(PacketBytes(State, PACKET_ID::SC_PLAYER_STATE));
	TestEqual(TEXT("Other player's private state rejected"), Net->GetPlayerState()->gold, 130);
	Net->ResetForTest();
	TestNull(TEXT("Session reset clears HUD"), Net->GetPlayerState());
	TestEqual(TEXT("Session reset clears visible HP"), HP->GetPercent(), 0.f);
	HUD->BindNetwork(nullptr);
	TestNull(TEXT("Session reset clears remote players"), Net->FindEntity(ENTITY_PLAYER, 0));
	Net->Receive(PacketBytes(Move, PACKET_ID::SC_MOVE_NOTIFY));
	TestNull(TEXT("Late move after leave ignored"), Net->FindEntity(ENTITY_PLAYER, 0));
	Wrapper.ForwardErrorMessages(this);
	return true;
}
#endif
