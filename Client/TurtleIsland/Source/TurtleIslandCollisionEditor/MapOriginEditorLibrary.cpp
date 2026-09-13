#include "MapOriginEditorLibrary.h"

#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "LevelEditorViewport.h"
#include "LevelUtils.h"
#include "NavigationData.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "WaterBodyActor.h"
#include "WaterBodyOceanComponent.h"

FString UMapOriginEditorLibrary::CenterIngameMapOnPlayerStart(bool bApply)
{
    const auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors;
    Report->SetBoolField(TEXT("applied"), false);
    const auto Finish = [&]()
    {
        Report->SetArrayField(TEXT("errors"), Errors);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        return Json;
    };
    const auto Fail = [&](const TCHAR* Message)
    {
        Errors.Add(MakeShared<FJsonValueString>(Message));
        return Finish();
    };
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || GEditor->PlayWorld || World->IsGameWorld()) return Fail(TEXT("Stop PIE and load the editor map."));
    if (World->GetOutermost()->GetName() != TEXT("/Game/Lv3_Ingame/Lv3_Ingame") || World->GetWorldPartition())
        return Fail(TEXT("Only the non-partitioned Lv3_Ingame map is supported."));
    for (ULevelStreaming* Streaming : World->GetStreamingLevels())
    {
        if (!Streaming || !Streaming->GetLoadedLevel() || !FLevelUtils::IsLevelVisible(Streaming->GetLoadedLevel()))
            return Fail(TEXT("Load and show every streaming sublevel before moving the map."));
        if (!Streaming->LevelTransform.Equals(FTransform::Identity))
            return Fail(TEXT("Non-identity streaming transforms must be handled before baking a map offset."));
    }
    if (World->GetLevels().Num() != 3 || World->GetStreamingLevels().Num() != 2)
        return Fail(TEXT("Expected Lv3_Ingame with Ingame_Env and Ingame_Gameplay loaded."));
    TArray<TSharedPtr<FJsonValue>> Levels;
    const TSet<FString> ExpectedLevels = {TEXT("/Game/Lv3_Ingame/Lv3_Ingame"),
        TEXT("/Game/Lv3_Ingame/SubLevels/Ingame_Env"), TEXT("/Game/Lv3_Ingame/SubLevels/Ingame_Gameplay")};
    for (ULevel* Level : World->GetLevels())
    {
        if (!ExpectedLevels.Contains(Level->GetOutermost()->GetName())) return Fail(TEXT("Unexpected sublevel; no changes made."));
        if (FLevelUtils::IsLevelLocked(Level)) return Fail(TEXT("Unlock all map levels first."));
        Levels.Add(MakeShared<FJsonValueString>(Level->GetOutermost()->GetName()));
    }
    Report->SetArrayField(TEXT("levels"), Levels);
    APlayerStart* Start = nullptr;
    for (TActorIterator<APlayerStart> It(World); It; ++It)
    {
        if (Start) return Fail(TEXT("Expected exactly one PlayerStart."));
        Start = *It;
    }
    if (!Start || Start->GetActorLocation().ContainsNaN()) return Fail(TEXT("Missing or invalid PlayerStart."));
    const FVector Offset(-Start->GetActorLocation().X, -Start->GetActorLocation().Y, 0);
    Report->SetNumberField(TEXT("offset_x"), Offset.X);
    Report->SetNumberField(TEXT("offset_y"), Offset.Y);
    Report->SetNumberField(TEXT("preserved_spawn_z"), Start->GetActorLocation().Z);
    TMap<TWeakObjectPtr<AActor>, FTransform> Before;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (It->GetRootComponent()) Before.Add(*It, It->GetActorTransform());
    Report->SetNumberField(TEXT("actors"), Before.Num());
    if (!bApply || Offset.IsNearlyZero()) return Finish();

    const FScopedTransaction Transaction(NSLOCTEXT("TurtleIsland", "CenterMap", "Center map on PlayerStart XY"));
    for (ULevel* Level : World->GetLevels())
    {
        Level->Modify();
        for (AActor* Actor : Level->Actors)
        {
            if (!IsValid(Actor)) continue;
            Actor->Modify();
            TInlineComponentArray<UActorComponent*> Components;
            Actor->GetComponents(Components);
            for (UActorComponent* Component : Components) Component->Modify();
        }
    }
    const FTransform Translation(Offset);
    // Engine level transforms update foliage instance data and absolute components too.
    // Defer construction/landscape updates until every level shares the same new frame.
    for (ULevel* Level : World->GetLevels())
    {
        FLevelUtils::FApplyLevelTransformParams Params(Level, Translation);
        Params.bDoPostEditMove = false;
        FLevelUtils::ApplyLevelTransform(Params);
        Level->MarkPackageDirty();
    }
    // Navigation data is derived, not scenery; keep its tile-aligned origin and rebuild it.
    for (const auto& Entry : Before)
        if (AActor* Actor = Entry.Key.Get(); Actor && Actor->IsA<ANavigationData>()) Actor->SetActorTransform(Entry.Value);
    for (ULevel* Level : World->GetLevels()) FLevelUtils::ApplyPostEditMove(Level);
    int32 RebuiltOceans = 0;
    for (TActorIterator<AWaterBody> It(World); It; ++It)
    {
        if (auto* Ocean = Cast<UWaterBodyOceanComponent>(It->GetWaterBodyComponent()))
        {
            // UE updates SavedZoneLocation AFTER generating mesh vertices. Two passes
            // refresh that cache, then build against it, without changing ocean extents.
            Ocean->UpdateWaterBodyRenderData();
            Ocean->UpdateWaterBodyRenderData();
            Ocean->MarkPackageDirty();
            ++RebuiltOceans;
        }
    }
    Report->SetNumberField(TEXT("rebuilt_oceans"), RebuiltOceans);
    int32 Checked = 0;
    for (const auto& Entry : Before)
    {
        const AActor* Actor = Entry.Key.Get();
        if (!Actor) continue; // Construction scripts can recreate child actors.
        const FVector ActorOffset = Actor->IsA<ANavigationData>() ? FVector::ZeroVector : Offset;
        const FTransform Expected(Entry.Value.GetRotation(), Entry.Value.GetLocation() + ActorOffset, Entry.Value.GetScale3D());
        if (!Actor->GetActorTransform().Equals(Expected, 0.1))
            Errors.Add(MakeShared<FJsonValueString>(TEXT("Unexpected transform after move: ") + Actor->GetPathName()));
        ++Checked;
    }
    for (FLevelEditorViewportClient* Viewport : GEditor->GetLevelViewportClients())
        if (Viewport && Viewport->GetWorld() == World) Viewport->SetViewLocation(Viewport->GetViewLocation() + Offset);
    GEditor->RedrawLevelEditingViewports();
    Report->SetBoolField(TEXT("applied"), true);
    Report->SetNumberField(TEXT("verified_actors"), Checked);
    Report->SetNumberField(TEXT("spawn_x"), Start->GetActorLocation().X);
    Report->SetNumberField(TEXT("spawn_y"), Start->GetActorLocation().Y);
    Report->SetStringField(TEXT("next"), TEXT("Inspect the report, rebuild navigation, verify terrain/water, save levels, then re-export collision and update Monster.lua."));
    return Finish();
}
