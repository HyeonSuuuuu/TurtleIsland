#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TurtleAudioSubsystem.generated.h"

class USoundBase;
class USoundAttenuation;

enum class ETurtleSound : uint8
{
	BasicSwing, SkillQ, SkillECast, SkillEImpact, HitMonster, HitPlayer, MonsterAttack, MonsterDeath,
	PlayerDeath, Respawn, LevelUp, Potion, Purchase, QuestAccept, QuestReady, QuestComplete, NpcReply,
	UIClick, WindowOpen, WindowClose, Count
};

/**
 *  효과음 재생 (Kenney CC0, /Game/Audio/SFX, Content/Python/import_sfx.py).
 *  어느 소리를 쓸지·음량은 cpp 의 표에 있다. 변형이 여러 개면 무작위로 고르고 음높이도 조금씩 흔든다.
 *  게임 창의 버튼을 누르면 어디서든 클릭음이 난다(WBP 마다 설정하지 않는다).
 */
UCLASS()
class TURTLEISLAND_API UTurtleAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Location 이 있으면 그 자리에서 거리 감쇠로, 없으면 2D 로 재생한다. */
	static void Play(const UObject* WorldContext, ETurtleSound Sound, const FVector* Location = nullptr);

private:
	void PlaySound(ETurtleSound Sound, const FVector* Location);
	bool IsGameWindow(const TSharedPtr<class SWindow>& Window) const;

	UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> Variants;
	TArray<TArray<int32>> VariantIndices; // per ETurtleSound, into Variants
	UPROPERTY(Transient) TObjectPtr<USoundAttenuation> WorldAttenuation;
	TSharedPtr<class IInputProcessor> ClickProcessor;
	friend class FTurtleClickSoundProcessor;
};
