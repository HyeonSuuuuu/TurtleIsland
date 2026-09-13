#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MapOriginEditorLibrary.generated.h"

UCLASS()
class TURTLEISLANDCOLLISIONEDITOR_API UMapOriginEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Preview by default. Requires a file backup and all Lv3_Ingame sublevels loaded.
    // Does not save packages or change server data. Z, rotation and scale are preserved.
    UFUNCTION(BlueprintCallable, Category="Map Origin")
    static FString CenterIngameMapOnPlayerStart(bool bApply = false);
};
