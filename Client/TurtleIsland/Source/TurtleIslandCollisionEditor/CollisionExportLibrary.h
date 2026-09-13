#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CollisionExportLibrary.generated.h"

UCLASS()
class TURTLEISLANDCOLLISIONEDITOR_API UCollisionExportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Server Collision")
    static bool IsCollisionExportAllowed();

    UFUNCTION(BlueprintPure, Category="Server Collision")
    static FString GetCollisionLevelPath();

    UFUNCTION(BlueprintCallable, Category="Server Collision")
    static FString AuditCollisionBehavior();

    UFUNCTION(BlueprintCallable, Category="Server Collision")
    static FString BuildCollisionScene(FVector SpawnLocation, float SpawnYaw, float GroundZ,
        FVector2D Origin, int32 Cols, int32 Rows, float CellSize = 100.f, float HeightRange = 200.f);

    UFUNCTION(BlueprintCallable, Category="Server Collision")
    static void PreviewCollisionScene(const FString& SceneJson, float Duration = 30.f);
};
