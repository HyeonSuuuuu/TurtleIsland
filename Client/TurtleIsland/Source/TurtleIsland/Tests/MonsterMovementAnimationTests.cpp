#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Character/ServerMonster.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/NetProxyMoverComponent.h"
#include "Network/NetSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterMovementAnimationTest,
	"TurtleIsland.Network.Npc.MovementAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonsterMovementAnimationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!TestWorld.BeginPlayInTestWorld()) return false;

	for (int32 Type = 0; Type < 3; ++Type)
	{
		const auto* ConfiguredClass = UNetSettings::Get()->NpcClasses.Find(Type);
		UClass* Class = ConfiguredClass ? ConfiguredClass->LoadSynchronous() : nullptr;
		if (!TestNotNull(TEXT("Configured monster class"), Class)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AServerMonster* Monster = World->SpawnActor<AServerMonster>(Class, FTransform::Identity, Spawn);
		if (!TestNotNull(TEXT("Monster spawned"), Monster)) return false;
		UNetProxyMoverComponent* Mover = Monster->FindComponentByClass<UNetProxyMoverComponent>();
		if (!TestNotNull(TEXT("Network mover"), Mover) ||
			!TestNotNull(TEXT("Single-node animation"), Monster->GetMesh()->GetSingleNodeInstance())) return false;
		Monster->MoveReferenceSpeed = 100.f;
		Monster->SetActorTickEnabled(false);
		Mover->SetComponentTickEnabled(false);
		auto Rate = [&] { return Monster->GetMesh()->GetSingleNodeInstance()->GetPlayRate(); };
		auto IsClip = [&](UAnimSequence* Clip) { return Monster->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() == Clip; };
		auto Sample = [&](float Speed, float DeltaSeconds)
		{
			Monster->GetCharacterMovement()->Velocity = FVector(Speed, 0.f, 0.f);
			Monster->Tick(DeltaSeconds);
		};
		auto Settle = [&](float Speed)
		{
			for (int32 Frame = 0; Frame < 100; ++Frame) Sample(Speed, 0.02f);
		};
		TestTrue(TEXT("Starts idle"), IsClip(Monster->IdleAnimation));
		Settle(50.f);
		TestTrue(TEXT("Movement clip at half reference speed"), IsClip(Monster->MoveAnimation));
		TestTrue(TEXT("Half-speed playback"), FMath::IsNearlyEqual(Rate(), 0.5f, 0.001f));
		Monster->GetMesh()->GetSingleNodeInstance()->SetPosition(0.1f, false);
		Sample(50.f, 0.02f);
		TestTrue(TEXT("Rate updates do not restart the movement clip"),
			FMath::IsNearlyEqual(Monster->GetMesh()->GetSingleNodeInstance()->GetCurrentTime(), 0.1f));
		Sample(150.f, 0.02f);
		TestTrue(TEXT("Acceleration is smoothed"), Rate() > 0.5f && Rate() < 1.5f);
		Settle(150.f);
		TestTrue(TEXT("Faster playback follows actual speed"), FMath::IsNearlyEqual(Rate(), 1.5f, 0.001f));
		Monster->MoveReferenceSpeed = 50.f;
		Settle(50.f);
		TestTrue(TEXT("Per-monster reference speed controls the ratio"), FMath::IsNearlyEqual(Rate(), 1.f, 0.001f));
		Monster->MoveReferenceSpeed = 100.f;
		Settle(0.f);
		TestTrue(TEXT("Stop switches to idle at normal speed"), IsClip(Monster->IdleAnimation) && Rate() == 1.f);
		Settle(5.f);
		TestTrue(TEXT("Idle does not flicker inside hysteresis band"), IsClip(Monster->IdleAnimation));
		Settle(8.f);
		Settle(5.f);
		TestTrue(TEXT("Moving stays moving inside hysteresis band"), IsClip(Monster->MoveAnimation));
		Settle(2.f);
		TestTrue(TEXT("Below stop threshold returns to idle"), IsClip(Monster->IdleAnimation));
		for (int32 Frame = 0; Frame < 100; ++Frame)
		{
			Monster->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, 1000.f);
			Monster->Tick(0.02f);
		}
		TestTrue(TEXT("Ground height changes do not drive movement animation"), IsClip(Monster->IdleAnimation));

		// Equal elapsed time gives the same filter response at different frame rates.
		Settle(0.f);
		for (int32 Frame = 0; Frame < 15; ++Frame) Sample(100.f, 1.f / 30.f);
		const float Rate30 = Rate();
		Settle(0.f);
		for (int32 Frame = 0; Frame < 60; ++Frame) Sample(100.f, 1.f / 120.f);
		TestTrue(TEXT("Smoothing is frame-rate independent"), FMath::IsNearlyEqual(Rate(), Rate30, 0.001f));
		const float BeforeInvalidTick = Rate();
		Sample(200.f, 0.f);
		TestEqual(TEXT("Zero delta time cannot corrupt playback"), Rate(), BeforeInvalidTick);

		// Exercise real 150ms server updates and the actual proxy interpolation/tick order.
		Monster->UpdateNetState(0.f, 0.f, 0.f);
		Monster->SetActorTickEnabled(true);
		Mover->SetComponentTickEnabled(true);
		TestWorld.TickTestWorld(1.f / 60.f);
		const float StepDistances[] = {4.f, 7.f, 5.f};
		float TargetX = 0.f, MinRate = MAX_flt, MaxRate = 0.f, MinSpeed = MAX_flt, MaxSpeed = 0.f, RateSum = 0.f;
		int32 Samples = 0;
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			if (Frame % 9 == 0)
			{
				TargetX += StepDistances[Type];
				Monster->UpdateNetState(TargetX, 0.f, 0.f);
			}
			TestWorld.TickTestWorld(1.f / 60.f);
			if (Frame < 120) continue;
			TestTrue(TEXT("Packet gaps do not switch a walking monster to idle"), IsClip(Monster->MoveAnimation));
			MinRate = FMath::Min(MinRate, Rate()); MaxRate = FMath::Max(MaxRate, Rate());
			MinSpeed = FMath::Min(MinSpeed, Mover->GetSpeed2D()); MaxSpeed = FMath::Max(MaxSpeed, Mover->GetSpeed2D());
			RateSum += Rate(); ++Samples;
		}
		TestTrue(TEXT("Smoothed rate has less packet ripple than raw velocity"),
			MaxRate - MinRate < (MaxSpeed - MinSpeed) / Monster->MoveReferenceSpeed * 0.75f);
		TestTrue(TEXT("Average rate follows server distance per second"),
			FMath::IsNearlyEqual(RateSum / Samples, StepDistances[Type] / 0.15f / Monster->MoveReferenceSpeed, 0.03f));
		Monster->UpdateNetState(TargetX + Mover->SnapDistance * 2.f, 0.f, 0.f);
		TestWorld.TickTestWorld(1.f / 60.f);
		TestTrue(TEXT("Network teleport is not treated as high-speed running"), IsClip(Monster->IdleAnimation) && Rate() == 1.f);
		Monster->SetActorTickEnabled(false);
		Mover->SetComponentTickEnabled(false);

		Settle(50.f);
		Monster->PlayAttack(45.f);
		TestTrue(TEXT("Attack resets locomotion playback rate"), IsClip(Monster->AttackAnimation) && Rate() == 1.f);
		Sample(200.f, 0.02f);
		TestTrue(TEXT("Moving cannot rescale or replace an active attack"), IsClip(Monster->AttackAnimation) && Rate() == 1.f);
		Monster->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		for (float Time = 0.f; Time < Monster->AttackAnimation->GetPlayLength() + 0.1f; Time += 0.02f)
		{
			TestWorld.TickTestWorld(0.02f);
			Monster->Tick(0.02f);
		}
		TestTrue(TEXT("Stopped attack returns to idle without stale speed"), IsClip(Monster->IdleAnimation) && Rate() == 1.f);
		Settle(50.f);
		UHealthComponent* Health = Monster->FindComponentByClass<UHealthComponent>();
		Health->ApplyServerDamage(40, 10, nullptr);
		TestTrue(TEXT("Hit resets locomotion playback rate"), IsClip(Monster->HitAnimation) && Rate() == 1.f);
		Sample(200.f, 0.02f);
		TestTrue(TEXT("Movement preserves active hit playback"), IsClip(Monster->HitAnimation) && Rate() == 1.f);
		Health->ApplyServerDamage(0, 40, nullptr);
		Sample(200.f, 0.02f);
		TestTrue(TEXT("Death remains normal speed and cannot return to movement"), IsClip(Monster->DeathAnimation) && Rate() == 1.f);
		Monster->Destroy();
	}
	TestWorld.DestroyTestWorld(false);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
