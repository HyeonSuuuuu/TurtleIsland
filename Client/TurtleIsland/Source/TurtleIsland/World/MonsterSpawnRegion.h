#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MonsterSpawnRegion.generated.h"

USTRUCT(BlueprintType)
struct FMonsterSpawnEntry
{
    GENERATED_BODY()

    // Zero-based index into Monster.lua's Monsters and Network Settings' NpcClasses.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Spawn", meta=(ClampMin="0", ClampMax="65535"))
    int32 MonsterType = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Spawn", meta=(ClampMin="1", ClampMax="8"))
    int32 Count = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Spawn", meta=(ClampMin="0.001", ClampMax="86400", Units="s"))
    double RespawnSeconds = 10.0;
};

/** Editor authoring marker only. The server owns spawning, death and respawn timers. */
UCLASS(NotBlueprintable, HideCategories=(Collision, Physics, Replication, Input, HLOD, Cooking))
class TURTLEISLAND_API AMonsterSpawnRegion : public AActor
{
    GENERATED_BODY()
public:
    AMonsterSpawnRegion();
    virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Spawn")
    bool bEnabled = true;

    // World XY is exported in centimeters. Actor Z is only for editor visibility.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Spawn", meta=(ClampMin="1", ClampMax="1000000", Units="cm"))
    float Radius = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Spawn", meta=(TitleProperty="MonsterType"))
    TArray<FMonsterSpawnEntry> Monsters;

private:
    void RefreshPreview();
#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TObjectPtr<class USplineComponent> Preview;
    UPROPERTY()
    TObjectPtr<class UBillboardComponent> Icon;
#endif
};
