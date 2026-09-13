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
	double ActionEndsAt = 0.;
	bool bAttacking = false;
	bool bDying = false;
};
