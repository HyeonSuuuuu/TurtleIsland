#include "Character/ServerMonster.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Gameplay/HealthComponent.h"
#include "Gameplay/NetProxyMoverComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

void AServerMonster::BeginPlay()
{
	Super::BeginPlay();
	ProxyMover = FindComponentByClass<UNetProxyMoverComponent>();
	if (ProxyMover) AddTickPrerequisiteComponent(ProxyMover);
	PlayClip(IdleAnimation, true);
}

void AServerMonster::PlayClip(UAnimSequence* Animation, bool bLoop)
{
	if (!Animation) return;
	CurrentClip = Animation;
	GetMesh()->PlayAnimation(Animation, bLoop);
	GetMesh()->SetPlayRate(1.f);
}

void AServerMonster::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDying || DeltaSeconds <= 0.f || !FMath::IsFinite(DeltaSeconds)) return;
	const float Speed = GetVelocity().Size2D();
	// A network snap is a placement correction, not a burst of running speed.
	if (!FMath::IsFinite(Speed) || (ProxyMover && Speed * DeltaSeconds > ProxyMover->SnapDistance))
	{
		SmoothedMoveSpeed = 0.f;
		bMoving = false;
	}
	else
	{
		const float Alpha = 1.f - FMath::Exp(-FMath::Max(MoveAnimationSmoothing, 0.01f) * DeltaSeconds);
		SmoothedMoveSpeed = FMath::Lerp(SmoothedMoveSpeed, Speed, Alpha);
		const float StopSpeed = FMath::Max(MoveStopSpeed, 0.f);
		const float StartSpeed = FMath::Max(MoveStartSpeed, StopSpeed + KINDA_SMALL_NUMBER);
		bMoving = bMoving ? SmoothedMoveSpeed > StopSpeed : SmoothedMoveSpeed > StartSpeed;
	}
	// Keep sampling during actions so an ended attack does not resume stale locomotion.
	if (GetWorld()->GetTimeSeconds() < ActionEndsAt) return;
	bAttacking = false;
	UAnimSequence* Clip = bMoving && MoveAnimation ? MoveAnimation.Get() : IdleAnimation.Get();
	if (Clip != CurrentClip) PlayClip(Clip, true);
	GetMesh()->SetPlayRate(bMoving && Clip == MoveAnimation && Clip
		? SmoothedMoveSpeed / FMath::Max(MoveReferenceSpeed, 1.f) : 1.f);
}

void AServerMonster::PlayAttack(float Yaw)
{
	if (bDying || Health->IsDead()) return;
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	PlayClip(AttackAnimation, false);
	bAttacking = true;
	ActionEndsAt = GetWorld()->GetTimeSeconds() + (AttackAnimation ? AttackAnimation->GetPlayLength() : 0.3f);
}

void AServerMonster::HandleDamaged(int32 Damage, float HPRatio, AActor* Causer)
{
	if (Health->IsDead() || bDying || Damage <= 0) return;
	// Do not visually cancel an attack that the server still considers active.
	if (!bAttacking || GetWorld()->GetTimeSeconds() >= ActionEndsAt)
	{
		PlayClip(HitAnimation, false);
		ActionEndsAt = GetWorld()->GetTimeSeconds() + (HitAnimation ? HitAnimation->GetPlayLength() : 0.f);
	}
	OnDamaged(static_cast<float>(Damage), HPRatio, Causer);
}

void AServerMonster::HandleDied(AActor* Killer)
{
	if (bDying) return;
	bDying = true;
	bAttacking = false;
	EnterDeadState();
	SetActorEnableCollision(false);
	GetCharacterMovement()->StopMovementImmediately();
	PlayClip(DeathAnimation, false);
	OnDied();
	// No GameMode respawn for server-owned NPCs. The registry drops the ID separately.
	SetLifeSpan((DeathAnimation ? DeathAnimation->GetPlayLength() : 0.f) + 0.25f);
}
