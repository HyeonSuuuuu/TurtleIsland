#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/CombatTestFixtures.h"
#include "Character/PlayerAgent.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/LocalMoverComponent.h"
#include "Gameplay/LocalMovementInput.h"
#include "Gameplay/PlayerInputComponent.h"
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
#include "UI/PlayerNameplate.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/UniformGridPanel.h"
#include "Components/CapsuleComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FThirdPersonCameraTest, "TurtleIsland.Gameplay.ThirdPersonCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FThirdPersonCameraTest::RunTest(const FString& Parameters)
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
	TestEqual(TEXT("Third-person distance"), Boom->TargetArmLength, 400.f);
	TestFalse(TEXT("Camera has no fixed world rotation"), Boom->IsUsingAbsoluteRotation());
	TestTrue(TEXT("Camera follows control rotation"), Boom->bUsePawnControlRotation);
	TestTrue(TEXT("Camera collision restored"), Boom->bDoCollisionTest);
	TestEqual(TEXT("Field of view"), Camera->FieldOfView, 90.f);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	ULocalMovementInput* MovementInput = Player->FindComponentByClass<ULocalMovementInput>();
	UPlayerInputComponent* Input = Player->FindComponentByClass<UPlayerInputComponent>();
	if (!TestNotNull(TEXT("Controller"), PC) || !TestNotNull(TEXT("Movement input"), MovementInput) ||
		!TestNotNull(TEXT("Player input"), Input)) return false;
	PC->SetAsLocalPlayerController();
	PC->Possess(Player);
	PC->SetControlRotation(FRotator(-20.f, 120.f, 0.f));
	TestEqual(TEXT("Camera uses control pitch"), Boom->GetTargetRotation().Pitch, -20.0);
	TestEqual(TEXT("Movement uses control yaw"), MovementInput->GetViewYaw(), 120.f);
	Input->OnMoveAxis.Broadcast(FVector2D(0.f, 1.f));
	const FVector Forward = FRotator(0.f, 120.f, 0.f).Vector();
	TestTrue(TEXT("Forward follows the current view"), MovementInput->GetMovementInput().Equals(FVector2D(Forward.X, Forward.Y)));
	Input->OnLookAxis.Broadcast(FVector2D(1.f, 1.f));
	TestFalse(TEXT("Mouse look produces yaw input"), FMath::IsNearlyZero(PC->RotationInput.Yaw));
	TestFalse(TEXT("Mouse look produces pitch input"), FMath::IsNearlyZero(PC->RotationInput.Pitch));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationSettingsTest, "TurtleIsland.Gameplay.AnimationSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationSettingsTest::RunTest(const FString& Parameters)
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
	Move->SetComponentTickEnabled(false);
	TestTrue(TEXT("BeginPlay preserves the animation blueprint root-motion setting"),
		Anim->RootMotionMode == Anim->GetClass()->GetDefaultObject<UAnimInstance>()->RootMotionMode);
	Agent->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	for (const ERootMotionMode::Type Mode : {ERootMotionMode::NoRootMotionExtraction, ERootMotionMode::IgnoreRootMotion,
		ERootMotionMode::RootMotionFromMontagesOnly, ERootMotionMode::RootMotionFromEverything})
	{
		for (int32 SkillId = 0; SkillId < PLAYER_SKILL_COUNT; ++SkillId)
		{
			UAnimMontage* Montage = Skills->SkillMontages.FindRef(SkillId);
			if (!TestNotNull(TEXT("Configured attack montage"), Montage)) return false;
			Anim->SetRootMotionMode(Mode);
			Agent->SetActorRotation(FRotator(12.f, 31.f, -8.f));
			Skills->PlaySkill(SkillId, 90.f);
			TestTrue(TEXT("Attack montage is playing"), Anim->Montage_IsPlaying(Montage));
			TestTrue(TEXT("Skill respects the chosen root-motion mode"), Anim->RootMotionMode == Mode);
			TestTrue(TEXT("Skill changes only server yaw, not pitch or roll"),
				Agent->GetActorRotation().Equals(FRotator(12.f, 90.f, -8.f)));
			TestTrue(TEXT("Authored animation settings retain skill input recovery"), Skills->IsMovementLocked());
			Wrapper.TickTestWorld();
			TestTrue(TEXT("Tick does not override animation settings"), Anim->RootMotionMode == Mode);
			Skills->ResetCast();
		}
	}
	Wrapper.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillMovementLockTest, "TurtleIsland.Gameplay.SkillMovementLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkillMovementLockTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ACombatTestGameMode::StaticClass();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	UClass* PlayerClass = LoadClass<APlayerAgent>(nullptr, TEXT("/Game/Lv3_Ingame/Paladin/BP_Paladin.BP_Paladin_C"));
	if (!TestNotNull(TEXT("Player blueprint"), PlayerClass)) return false;
	APlayerAgent* Player = World->SpawnActor<APlayerAgent>(PlayerClass);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Controller"), PC)) return false;
	PC->SetAsLocalPlayerController();
	PC->Possess(Player);
	UAgentSkillComponent* Skills = Player->FindComponentByClass<UAgentSkillComponent>();
	ULocalMoverComponent* Mover = Player->FindComponentByClass<ULocalMoverComponent>();
	UPlayerInputComponent* Input = Player->FindComponentByClass<UPlayerInputComponent>();
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	if (!TestNotNull(TEXT("Skills"), Skills) || !TestNotNull(TEXT("Mover"), Mover) ||
		!TestNotNull(TEXT("Input"), Input) || !TestNotNull(TEXT("Animation instance"), Anim)) return false;
	// Test input recovery with in-place playback, independently of authored root motion.
	Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	UCharacterMovementComponent* Move = Player->GetCharacterMovement();
	Move->SetMovementMode(MOVE_Flying);
	Move->SetComponentTickEnabled(false);
	Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Input->OnMoveAxis.Broadcast(FVector2D(0.f, 1.f));
	for (int32 SkillId = 0; SkillId < PLAYER_SKILL_COUNT; ++SkillId)
	{
		UAnimMontage* Montage = Skills->SkillMontages.FindRef(SkillId);
		if (!TestNotNull(TEXT("Configured skill montage"), Montage)) return false;
		const float Duration = GameplayRules::Skills[SkillId].movementLockMs / 1000.f;
		TestTrue(TEXT("Shared recovery covers the default montage"),
			Montage->GetPlayLength() / (Skills->PlayRate * Montage->RateScale) <= Duration + 0.001f);
		Move->Velocity = FVector(500.f, 120.f, 250.f);
		Player->AddMovementInput(FVector::ForwardVector, 1.f, true);
		Player->Jump();
		Skills->PlaySkill(SkillId, 90.f);
		TestTrue(TEXT("Server-approved cast starts playback"), Anim->Montage_IsPlaying(Montage));
		TestTrue(TEXT("Every skill locks movement"), Mover->IsMovementLocked());
		TestEqual(TEXT("Cast immediately stops horizontal drift"), Move->Velocity.Size2D(), 0.0);
		TestEqual(TEXT("Cast preserves airborne velocity"), Move->Velocity.Z, 250.0);
		TestTrue(TEXT("Cast clears buffered movement"), Player->GetPendingMovementInputVector().IsNearlyZero());
		TestFalse(TEXT("Cast cancels buffered jump"), Player->bPressedJump);
		Input->OnJumpPressed.Broadcast();
		TestFalse(TEXT("New jump input is blocked during cast"), Player->bPressedJump);
		Move->Velocity = FVector(500.f, 0.f, -100.f);
		Mover->TickComponent(0.01f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Held movement cannot restart drift"), Move->Velocity.Size2D(), 0.0);
		TestEqual(TEXT("Gravity remains independent of skill lock"), Move->Velocity.Z, -100.0);
		TestTrue(TEXT("Held movement does not add input"), Player->GetPendingMovementInputVector().IsNearlyZero());
		// Q also exercises an interrupted montage: visual cancellation cannot shorten server recovery.
		if (SkillId == 1) Anim->Montage_Stop(0.f, Montage);
		Move->Velocity = FVector::ZeroVector;
		Move->SetComponentTickEnabled(true);
		const FVector CastPosition = Player->GetActorLocation();
		const double Deadline = World->GetTimeSeconds() + Duration;
		while (World->GetTimeSeconds() + 0.01 < Deadline)
		{
			Wrapper.TickTestWorld(0.01f);
			TestTrue(TEXT("Movement remains locked through cast recovery"), Mover->IsMovementLocked());
			TestTrue(TEXT("Character physics cannot move a casting player with held input"), Player->GetActorLocation().Equals(CastPosition));
		}
		for (int32 Frame = 0; Frame < 4; ++Frame) Wrapper.TickTestWorld(0.01f);
		Move->SetComponentTickEnabled(false);
		TestFalse(TEXT("Movement unlocks when cast finishes"), Mover->IsMovementLocked());
		Mover->TickComponent(0.01f, LEVELTICK_All, nullptr);
		TestFalse(TEXT("Held movement resumes without another key press"), Player->GetPendingMovementInputVector().IsNearlyZero());
		Input->OnJumpPressed.Broadcast();
		TestTrue(TEXT("Jump resumes after recovery"), Player->bPressedJump);
		Player->StopJumping();
		Player->ConsumeMovementInputVector();
	}

	Skills->PlaySkill(-1, 0.f);
	Skills->PlaySkill(PLAYER_SKILL_COUNT, 0.f);
	TestFalse(TEXT("Invalid skill cannot create a movement lock"), Mover->IsMovementLocked());
	Skills->PlayRate = 0.5f;
	Skills->PlaySkill(2, 0.f);
	for (int32 Frame = 0; Frame < 140; ++Frame) Wrapper.TickTestWorld(0.01f);
	TestTrue(TEXT("Slower montage remains locked past minimum recovery"), Mover->IsMovementLocked());
	Skills->ResetCast();
	TestFalse(TEXT("Explicit reset stops montage and recovery"), Mover->IsMovementLocked());
	Skills->SkillMontages.Empty();
	Skills->PlaySkill(1, 0.f);
	TestTrue(TEXT("Missing visual assets still enforce server recovery"), Mover->IsMovementLocked());
	for (int32 Frame = 0; Frame < 170; ++Frame) Wrapper.TickTestWorld(0.01f);
	TestFalse(TEXT("Missing assets cannot leave a permanent lock"), Mover->IsMovementLocked());
	Skills->PlaySkill(1, 0.f);
	Player->FindComponentByClass<UHealthComponent>()->ApplyServerDeath(nullptr);
	TestFalse(TEXT("Death clears cast state"), Skills->IsMovementLocked());
	Skills->PlaySkill(1, 0.f);
	TestFalse(TEXT("Dead player cannot start a cast lock"), Skills->IsMovementLocked());
	Player->ResetForRespawn(FVector::ZeroVector, 100);
	TestFalse(TEXT("Respawn is not held by the previous cast"), Skills->IsMovementLocked());
	Skills->PlaySkill(1, 0.f);
	Player->LandedDelegate.Broadcast(FHitResult());
	Skills->ResetCast();
	TestTrue(TEXT("Clearing skill recovery does not clear landing recovery"), Mover->IsMovementLocked());
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
	const uint64_t CastReadyAt = 100 + GameplayRules::Skills[1].movementLockMs;
	TestEqual(TEXT("Skill starts shared cast recovery"), State.castReadyAt, CastReadyAt);
	TestTrue(TEXT("Different skill cannot interrupt recovery"), GameplayRules::UseSkill(State, 2, CastReadyAt - 1) == ActionResult::Cooldown);
	TestEqual(TEXT("Rejected cast does not extend recovery"), State.castReadyAt, CastReadyAt);
	TestTrue(TEXT("Cooldown rejects duplicate"), GameplayRules::UseSkill(State, 1, 101) == ActionResult::Cooldown);
	TestEqual(TEXT("Rejected cast keeps mana"), State.mp, 80);
	State.mp = 0;
	TestTrue(TEXT("Insufficient mana"), GameplayRules::UseSkill(State, 2, 5000) == ActionResult::NotEnoughMP);
	TestTrue(TEXT("Unknown skill"), GameplayRules::UseSkill(State, 3, 5000) == ActionResult::Invalid);
	TestEqual(TEXT("Invalid and insufficient-MP casts leave recovery unchanged"), State.castReadyAt, CastReadyAt);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerRespawnTest, "TurtleIsland.Network.PlayerRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerRespawnTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ACombatTestGameMode::StaticClass();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	UClass* PlayerClass = LoadClass<APlayerAgent>(nullptr, TEXT("/Game/Lv3_Ingame/Paladin/BP_Paladin.BP_Paladin_C"));
	if (!TestNotNull(TEXT("Player blueprint"), PlayerClass)) return false;
	APlayerAgent* Player = World->SpawnActor<APlayerAgent>(PlayerClass);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Local player"), Player) || !TestNotNull(TEXT("Controller"), PC)) return false;
	PC->SetAsLocalPlayerController(); PC->Possess(Player);
	UCombatTestNetSubsystem* Net = NewObject<UCombatTestNetSubsystem>(World->GetGameInstance());
	Net->Configure(World, 42);
	World->GetSubsystem<UNpcSubsystem>()->SetReady(true);
	UHealthComponent* Health = Player->FindComponentByClass<UHealthComponent>();
	UAgentSkillComponent* Skills = Player->FindComponentByClass<UAgentSkillComponent>();
	ULocalMoverComponent* Mover = Player->FindComponentByClass<ULocalMoverComponent>();
	UPlayerInputComponent* Input = Player->FindComponentByClass<UPlayerInputComponent>();
	UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	if (!Health || !Skills || !Mover || !Input || !Movement) return false;
	Movement->SetComponentTickEnabled(false);
	SC_MoveNotify Pose{}; Pose.userIndex = 42; Pose.x = 100; Pose.y = 200; Pose.z = 96; Pose.hp = Pose.maxHP = 100;
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	Input->OnMoveAxis.Broadcast(FVector2D(0, 1));
	Skills->PlaySkill(1, 90.f);
	Player->LandedDelegate.Broadcast(FHitResult());
	Player->Jump();
	Player->AddMovementInput(FVector::ForwardVector, 1.f, true);
	Movement->Velocity = FVector(500, 100, -300);
	SC_DamageNotify Damage{}; Damage.targetKind = ENTITY_PLAYER; Damage.targetId = 42;
	Damage.damage = 100; Damage.maxHP = 100;
	Net->Receive(PacketBytes(Damage, PACKET_ID::SC_DAMAGE_NOTIFY));
	TestTrue(TEXT("Lethal server damage kills owner"), Health->IsDead());
	TestTrue(TEXT("Dead movement is locked"), Mover->IsMovementLocked());
	TestTrue(TEXT("Dead physics disabled"), Movement->MovementMode == MOVE_None);
	TestTrue(TEXT("Dead capsule disabled"), Player->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Death locks controller input"), PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
	Input->OnJumpPressed.Broadcast();
	TestFalse(TEXT("Dead player cannot jump"), Player->bPressedJump);
	Pose.hp = 0;
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	TestTrue(TEXT("Dead pose correction cannot enable physics"), Movement->MovementMode == MOVE_None);
	SC_RespawnNotify Respawn{}; Respawn.userIndex = 42; Respawn.lifeId = 1;
	Respawn.hp = Respawn.maxHP = 100; Respawn.yaw = 30.f;
	Net->Receive(PacketBytes(Respawn, PACKET_ID::SC_RESPAWN_NOTIFY));
	TestFalse(TEXT("Server respawn revives owner"), Health->IsDead());
	TestEqual(TEXT("Server restores HP"), Health->CurrentHP, 100);
	TestTrue(TEXT("World origin is a valid respawn destination"), Player->GetActorLocation().IsNearlyZero());
	TestTrue(TEXT("Respawn restores facing"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, 30.0));
	TestTrue(TEXT("Respawn restores walking"), Movement->MovementMode == MOVE_Walking);
	TestTrue(TEXT("Respawn restores collision"), Player->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
	TestFalse(TEXT("Respawn releases death input lock"), PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
	TestFalse(TEXT("Respawn clears cast and landing locks"), Mover->IsMovementLocked());
	TestTrue(TEXT("Respawn stops velocity and buffered movement"), Movement->Velocity.IsNearlyZero() && Player->GetPendingMovementInputVector().IsNearlyZero());
	Mover->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Old held key cannot restart movement after respawn"), Player->GetPendingMovementInputVector().IsNearlyZero());
	TestFalse(TEXT("Respawn clears held jump"), Player->bPressedJump);
	TestEqual(TEXT("Outgoing requests use the current life"), Net->GetPlayerLifeId(), uint32(1));
	SC_PlayerState State{}; State.playerId = 42; State.lifeId = 1; State.hp = State.maxHP = 100;
	State.mp = State.maxMP = 120; State.level = 4; State.expToNext = 250; State.gold = 123; State.moveSpeed = 500;
	State.items[0] = {1, 3};
	Net->Receive(PacketBytes(State, PACKET_ID::SC_PLAYER_STATE));
	if (!TestNotNull(TEXT("Respawn HUD state"), Net->GetPlayerState())) return false;
	TestEqual(TEXT("HUD receives full MP"), Net->GetPlayerState()->mp, 120);
	TestEqual(TEXT("HUD preserves gold"), Net->GetPlayerState()->gold, 123);
	Player->SetActorLocation(FVector(20, 30, 96));
	Net->Receive(PacketBytes(Respawn, PACKET_ID::SC_RESPAWN_NOTIFY));
	Pose.hp = 0;
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	State.lifeId = 0; State.hp = 0; State.gold = 0;
	Net->Receive(PacketBytes(State, PACKET_ID::SC_PLAYER_STATE));
	TestTrue(TEXT("Duplicate respawn and old-life poses cannot teleport owner"), Player->GetActorLocation().Equals(FVector(20, 30, 96)));
	TestFalse(TEXT("Old-life state cannot kill revived owner"), Health->IsDead());
	TestEqual(TEXT("Old-life state cannot overwrite HUD"), Net->GetPlayerState()->gold, 123);

	PC->SetIgnoreMoveInput(true); PC->SetIgnoreLookInput(true);
	Net->Receive(PacketBytes(Damage, PACKET_ID::SC_DAMAGE_NOTIFY));
	Respawn.lifeId = 2;
	Net->Receive(PacketBytes(Respawn, PACKET_ID::SC_RESPAWN_NOTIFY));
	TestTrue(TEXT("Respawn preserves independent UI input locks"), PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
	Net->Receive(PacketBytes(Respawn, PACKET_ID::SC_RESPAWN_NOTIFY));
	TestTrue(TEXT("Duplicate respawn cannot decrement UI locks"), PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
	PC->SetIgnoreMoveInput(false); PC->SetIgnoreLookInput(false);
	TestFalse(TEXT("Death lock was released exactly once"), PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());

	Pose.userIndex = 7; Pose.lifeId = 0; Pose.hp = 100;
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	AServerAgent* Remote = Cast<AServerAgent>(Net->FindEntity(ENTITY_PLAYER, 7));
	if (!TestNotNull(TEXT("Remote player"), Remote)) return false;
	Wrapper.TickTestWorld(0.01f);
	Damage.targetId = 7;
	Net->Receive(PacketBytes(Damage, PACKET_ID::SC_DAMAGE_NOTIFY));
	TestTrue(TEXT("Remote death visible"), Remote->FindComponentByClass<UHealthComponent>()->IsDead());
	Respawn.userIndex = 7; Respawn.lifeId = 1; Respawn.x = 120; Respawn.y = 220; Respawn.z = 96;
	Net->Receive(PacketBytes(Respawn, PACKET_ID::SC_RESPAWN_NOTIFY));
	const FVector RemoteSpawn(120, 220, 96);
	TestTrue(TEXT("Nearby respawn teleports without interpolation"), Remote->GetActorLocation().Equals(RemoteSpawn));
	TestFalse(TEXT("Remote revived"), Remote->FindComponentByClass<UHealthComponent>()->IsDead());
	Wrapper.TickTestWorld(0.01f);
	TestTrue(TEXT("Proxy target reset to spawn"), Remote->GetActorLocation().Equals(RemoteSpawn));
	TestTrue(TEXT("Same remote actor reused"), Net->FindEntity(ENTITY_PLAYER, 7) == Remote);
	TestFalse(TEXT("Remote physics stays disabled"), Remote->GetCharacterMovement()->IsComponentTickEnabled());
	Pose.hp = 0;
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	TestFalse(TEXT("Old remote life cannot kill new one"), Remote->FindComponentByClass<UHealthComponent>()->IsDead());
	Net->ResetForTest();
	TestEqual(TEXT("Session reset clears life ID"), Net->GetPlayerLifeId(), uint32(0));
	Net->Receive(PacketBytes(Respawn, PACKET_ID::SC_RESPAWN_NOTIFY));
	TestNull(TEXT("Late respawn cannot recreate player after leave"), Net->FindEntity(ENTITY_PLAYER, 7));
	Wrapper.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerNameplateTest, "TurtleIsland.UI.PlayerNameplates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerNameplateTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Wrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ACombatTestGameMode::StaticClass();
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	UClass* PlayerClass = LoadClass<APlayerAgent>(nullptr, TEXT("/Game/Lv3_Ingame/Paladin/BP_Paladin.BP_Paladin_C"));
	if (!TestNotNull(TEXT("Player blueprint"), PlayerClass)) return false;
	APlayerAgent* Local = World->SpawnActor<APlayerAgent>(PlayerClass);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	if (!Local || !PC) return false;
	PC->SetAsLocalPlayerController(); PC->Possess(Local);
	UCombatTestNetSubsystem* Net = NewObject<UCombatTestNetSubsystem>(World->GetGameInstance());
	Net->Configure(World, 42);
	Net->SetUserID(TEXT("Owner"));
	UNpcSubsystem* Npcs = World->GetSubsystem<UNpcSubsystem>();
	Npcs->SetReady(true);
	FString Name;
	int32 Level = 0;
	TestTrue(TEXT("Owner identified before private snapshot"), Net->GetPlayerDisplayInfo(Local, Name, Level));
	TestEqual(TEXT("Local name fallback"), Name, FString(TEXT("Owner")));
	TestEqual(TEXT("Unknown online level is not invented"), Level, 0);
	SC_PlayerInfo Info{}; Info.playerId = 7; Info.level = 8;
	const FString UnicodeName(TEXT("\uac70\ubd81\uc774"));
	Net->CopyStringToBuffer(Info.userName, sizeof(Info.userName), UnicodeName);
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	TestNull(TEXT("Identity does not spawn an actor"), Net->FindAgentById(7));
	TestNull(TEXT("Public identity does not create private HUD state"), Net->GetPlayerState());
	SC_MoveNotify Pose{}; Pose.userIndex = 7; Pose.x = 300; Pose.z = 96; Pose.hp = 80; Pose.maxHP = 120;
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	ATurtleAgent* Remote = Net->FindAgentById(7);
	if (!TestNotNull(TEXT("Remote created"), Remote)) return false;
	TestTrue(TEXT("Early identity binds to later actor"), Net->GetPlayerDisplayInfo(Remote, Name, Level));
	TestEqual(TEXT("UTF-8 name decoded"), Name, UnicodeName);
	TestEqual(TEXT("Public level applied"), Level, 8);
	TestEqual(TEXT("Nameplate health comes from pose"), Remote->FindComponentByClass<UHealthComponent>()->CurrentHP, 80);
	Info.level = 9;
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	Net->GetPlayerDisplayInfo(Remote, Name, Level);
	TestEqual(TEXT("Level-up updates existing actor"), Level, 9);
	SC_HealthState Health{}; Health.playerId = 7; Health.hp = Health.maxHP = 140;
	Net->Receive(PacketBytes(Health, PACKET_ID::SC_HEALTH_STATE));
	TestEqual(TEXT("Nameplate max HP updates on level-up"), Remote->FindComponentByClass<UHealthComponent>()->MaxHP, 140);
	SC_DamageNotify Damage{}; Damage.targetKind = ENTITY_PLAYER; Damage.targetId = 7;
	Damage.damage = 140; Damage.maxHP = 140;
	Net->Receive(PacketBytes(Damage, PACKET_ID::SC_DAMAGE_NOTIFY));
	TestEqual(TEXT("Nameplate shows zero on death"), Remote->FindComponentByClass<UHealthComponent>()->CurrentHP, 0);
	SC_RespawnNotify Respawn{}; Respawn.userIndex = 7; Respawn.lifeId = 1; Respawn.hp = Respawn.maxHP = 140;
	Net->Receive(PacketBytes(Respawn, PACKET_ID::SC_RESPAWN_NOTIFY));
	Info.lifeId = 1; Info.level = 10;
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	Info.lifeId = 0; Info.level = 2;
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	Net->GetPlayerDisplayInfo(Remote, Name, Level);
	TestEqual(TEXT("Old life cannot overwrite nameplate"), Level, 10);
	TestEqual(TEXT("Respawn restores nameplate HP"), Remote->FindComponentByClass<UHealthComponent>()->CurrentHP, 140);
	Info.lifeId = 1; Info.level = 0;
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	Info.level = 2; FMemory::Memset(Info.userName, 'A', sizeof(Info.userName));
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	TArray<uint8> Truncated = PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO); Truncated.Pop();
	reinterpret_cast<PacketHeader*>(Truncated.GetData())->size = Truncated.Num();
	AddExpectedErrorPlain(TEXT("[Codec] Packet size mismatch."), EAutomationExpectedErrorFlags::Contains, 1);
	Net->Receive(Truncated);
	Net->GetPlayerDisplayInfo(Remote, Name, Level);
	TestEqual(TEXT("Malformed identities leave valid state intact"), Level, 10);
	SC_NpcSpawn Spawn{}; Spawn.npcId = 7; Spawn.hp = Spawn.maxHp = 100;
	Net->Receive(PacketBytes(Spawn, PACKET_ID::SC_NPC_SPAWN));
	TestFalse(TEXT("NPC cannot inherit overlapping player identity"), Net->GetPlayerDisplayInfo(Net->FindEntity(ENTITY_NPC, 7), Name, Level));
	SC_RoomLeaveNotify Leave{}; Leave.userIndex = 7;
	Net->Receive(PacketBytes(Leave, PACKET_ID::SC_ROOM_LEAVE_NOTIFY));
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	Net->GetPlayerDisplayInfo(Net->FindAgentById(7), Name, Level);
	TestEqual(TEXT("Leave clears reused ID level"), Level, 0);
	Net->CopyStringToBuffer(Info.userName, sizeof(Info.userName), UnicodeName);
	Info.lifeId = 0; Info.level = 8;
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	Net->ResetForTest();
	Net->Receive(PacketBytes(Info, PACKET_ID::SC_PLAYER_INFO));
	Npcs->SetReady(true);
	Net->Receive(PacketBytes(Pose, PACKET_ID::SC_MOVE_NOTIFY));
	Net->GetPlayerDisplayInfo(Net->FindAgentById(7), Name, Level);
	TestEqual(TEXT("Session reset discards identity and late packets"), Level, 0);
	Net->SetSingleForTest();
	Local->FindComponentByClass<UPlayerStatsComponent>()->Level = 3;
	Net->GetPlayerDisplayInfo(Local, Name, Level);
	TestEqual(TEXT("Single-player level uses local stats"), Level, 3);
	TestEqual(TEXT("Single-player name visible without server state"), Name, FString(TEXT("Owner")));
	const auto Short = PlayerNameplate::FitText(TEXT("Lv. 3  Owner"), PlayerNameplate::NameWidth, 14, 11);
	TestEqual(TEXT("Short names keep normal size"), Short.Font.Size, 14.f);
	TestEqual(TEXT("Short name unchanged"), Short.Text, FString(TEXT("Lv. 3  Owner")));
	const auto Long = PlayerNameplate::FitText(FString::ChrN(32, TEXT('W')), PlayerNameplate::NameWidth, 14, 11);
	TestTrue(TEXT("Long names fit fixed width"), Long.Size.X <= PlayerNameplate::NameWidth);
	TestTrue(TEXT("Long name ellipsis"), Long.Text.EndsWith(TEXT("...")));
	const auto Korean = PlayerNameplate::FitText(UnicodeName, PlayerNameplate::NameWidth, 14, 11);
	TestEqual(TEXT("Korean text preserved"), Korean.Text, UnicodeName);
	const auto SingleLine = PlayerNameplate::FitText(TEXT("A\nB\r\tC"), PlayerNameplate::NameWidth, 14, 11);
	TestEqual(TEXT("Name cannot inject lines"), SingleLine.Text, FString(TEXT("A B  C")));
	const auto LargeHP = PlayerNameplate::FitText(TEXT("2147483647 / 2147483647"), PlayerNameplate::BarWidth - 8, 10, 8);
	TestTrue(TEXT("HP text stays inside bar"), LargeHP.Size.X <= PlayerNameplate::BarWidth - 8 && LargeHP.Size.Y <= PlayerNameplate::BarHeight);
	Net->ResetForTest();
	Wrapper.ForwardErrorMessages(this);
	return true;
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
	UAgentSkillComponent* LocalSkills = Local->FindComponentByClass<UAgentSkillComponent>();
	SC_ActionResult RejectedCast{};
	RejectedCast.requestId = static_cast<uint16>(PACKET_ID::CS_ATTACK);
	RejectedCast.actionId = 1; RejectedCast.result = ActionResult::NotEnoughMP;
	Net->Receive(PacketBytes(RejectedCast, PACKET_ID::SC_ACTION_RESULT));
	TestFalse(TEXT("Server rejection cannot lock local movement"), LocalSkills->IsMovementLocked());
	SC_AttackNotify Cast{};
	Cast.casterId = 42; Cast.casterKind = ENTITY_PLAYER; Cast.skillId = 1; Cast.yaw = 90.f;
	Net->Receive(PacketBytes(Cast, PACKET_ID::SC_ATTACK_NOTIFY));
	TestTrue(TEXT("Server approval locks the local caster without montage assets"), LocalSkills->IsMovementLocked());
	Net->Receive(PacketBytes(Correction, PACKET_ID::SC_MOVE_NOTIFY));
	TestTrue(TEXT("Position correction cannot clear skill recovery"), LocalSkills->IsMovementLocked());
	Cast.casterId = 0; Cast.skillId = 2;
	Net->Receive(PacketBytes(Cast, PACKET_ID::SC_ATTACK_NOTIFY));
	TestTrue(TEXT("Remote E follows the same approved cast path"), Remote->FindComponentByClass<UAgentSkillComponent>()->IsMovementLocked());
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
