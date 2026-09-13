#pragma once

#include "CoreMinimal.h"
#include "Character/ServerAgent.h"
#include "ServerMonster.generated.h"

class UAnimSequence;

// Server NPC presentation only; never owns damage, AI, or respawn decisions.
UCLASS()
class TURTLEISLAND_API AServerMonster : public AServerAgent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Monster") FText MonsterDisplayName;
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation") TObjectPtr<UAnimSequence> IdleAnimation;
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation") TObjectPtr<UAnimSequence> MoveAnimation;
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation") TObjectPtr<UAnimSequence> AttackAnimation;
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation") TObjectPtr<UAnimSequence> HitAnimation;
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation") TObjectPtr<UAnimSequence> DeathAnimation;
	/** World speed (cm/s) that matches the movement clip at 1x, including mesh scale. */
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation|Movement", meta=(ClampMin="1.0", Units="cm/s"))
	float MoveReferenceSpeed = 100.f;
	/** Response rate in inverse seconds. Higher values follow speed changes faster. */
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation|Movement", meta=(ClampMin="0.01"))
	float MoveAnimationSmoothing = 8.f;
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation|Movement", meta=(ClampMin="0.0", Units="cm/s"))
	float MoveStartSpeed = 7.f;
	UPROPERTY(EditDefaultsOnly, Category="Monster|Animation|Movement", meta=(ClampMin="0.0", Units="cm/s"))
	float MoveStopSpeed = 3.f;
	void PlayAttack(float Yaw);
	bool IsPlayingDeath() const { return bDying; }
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
	virtual void HandleDamaged(int32 Damage, float HPRatio, AActor* Causer) override;
	virtual void HandleDied(AActor* Killer) override;
private:
	void PlayClip(UAnimSequence* Animation, bool bLoop);
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentClip;
	UPROPERTY(Transient) TObjectPtr<UNetProxyMoverComponent> ProxyMover;
	double ActionEndsAt = 0.;
	float SmoothedMoveSpeed = 0.f;
	bool bMoving = false;
	bool bAttacking = false;
	bool bDying = false;
};
