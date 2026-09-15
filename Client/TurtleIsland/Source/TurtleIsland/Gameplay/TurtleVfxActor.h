#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TurtleVfxActor.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

enum class ETurtleVfx : uint8 { Hit, HitPlayer, LevelUp, PotionHP, PotionMP, Death, Respawn, Purchase };

/**
 *  에셋 없이 쓰는 간단한 이펙트. /Game/VFX 텍스처를 가산 머티리얼 평면에 띄워
 *  크기·회전·투명도를 짧게 애니메이션하고 스스로 사라진다. 프리셋은 cpp 의 표에 있다.
 */
UCLASS(NotBlueprintable)
class TURTLEISLAND_API ATurtleVfxActor : public AActor
{
	GENERATED_BODY()
public:
	ATurtleVfxActor();
	static void Spawn(UWorld* World, ETurtleVfx Kind, const FVector& Location, float Yaw = 0.f);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FLayer
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		TObjectPtr<UMaterialInstanceDynamic> Material;
		int32 Def = 0;
	};
	void Build(ETurtleVfx InKind);
	ETurtleVfx Kind = ETurtleVfx::Hit;
	TArray<FLayer> Layers;
	float Age = 0.f;
	float Duration = 0.f;
};
