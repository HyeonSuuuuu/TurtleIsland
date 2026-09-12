#include "Character/ServerMonster.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Gameplay/HealthComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

void AServerMonster::BeginPlay()
{
	Super::BeginPlay();
	PlayClip(IdleAnimation, true);
}

void AServerMonster::PlayClip(UAnimSequence* Animation, bool bLoop)
{
	if (!Animation) return;
	CurrentClip = Animation;
	GetMesh()->PlayAnimation(Animation, bLoop);
}

void AServerMonster::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDying || GetWorld()->GetTimeSeconds() < ActionEndsAt) return;
	bAttacking = false;
	UAnimSequence* Clip = GetVelocity().SizeSquared2D() > 25.f ? MoveAnimation.Get() : IdleAnimation.Get();
	if (Clip != CurrentClip) PlayClip(Clip, true);
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
