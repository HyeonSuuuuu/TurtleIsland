#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MonsterSpawnExportLibrary.generated.h"

UCLASS()
class TURTLEISLANDCOLLISIONEDITOR_API UMonsterSpawnExportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Monster Spawn")
    static FString CollectMonsterSpawnRegions();
};
