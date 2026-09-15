#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponTrailActor.generated.h"

class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USkeletalMeshComponent;

/**
 *  검을 따라가는 바람결 트레일. 휘두르는 구간 동안 칼날 손잡이·끝 위치를 매 프레임 기록해
 *  리본 메시로 잇고, 오래된 샘플부터 흐려진다(T_VFX_WindTrail 의 U 가 샘플 나이).
 */
UCLASS(NotBlueprintable)
class TURTLEISLAND_API AWeaponTrailActor : public AActor
{
	GENERATED_BODY()
public:
	AWeaponTrailActor();
	/** Delay/Duration: seconds from cast start while the blade is sampled. BaseOffset/TipOffset are in the bone's space. */
	static void Spawn(USkeletalMeshComponent* Mesh, FName Bone, const FVector& BaseOffset, const FVector& TipOffset,
		float Delay, float Duration, const FLinearColor& Colour);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FSample { FVector Base, Tip; float Time; };
	void Sample();
	void Rebuild();
	TWeakObjectPtr<USkeletalMeshComponent> Source;
	FName Bone;
	FVector BaseOffset, TipOffset;
	float Delay = 0.f, Duration = 0.f, Age = 0.f;
	TArray<FSample> Samples;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Ribbon;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	static constexpr float FadeSeconds = 0.22f;
};
