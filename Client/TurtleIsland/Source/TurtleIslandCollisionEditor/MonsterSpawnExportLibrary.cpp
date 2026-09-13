#include "MonsterSpawnExportLibrary.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "Network/NetSettings.h"
#include "Serialization/JsonSerializer.h"
#include "World/MonsterSpawnRegion.h"

FString UMonsterSpawnExportLibrary::CollectMonsterSpawnRegions()
{
    const auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors, Regions, Types;
    const auto Finish = [&]()
    {
        Report->SetArrayField(TEXT("errors"), Errors);
        Report->SetArrayField(TEXT("regions"), Regions);
        Report->SetArrayField(TEXT("client_types"), Types);
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
    if (!World || GEditor->PlayWorld || World->IsGameWorld()) return Fail(TEXT("Stop PIE before exporting."));
    Report->SetStringField(TEXT("source_level"), World->GetOutermost()->GetName());
    if (World->GetOutermost()->GetName() != TEXT("/Game/Lv3_Ingame/Lv3_Ingame") || World->GetWorldPartition())
        return Fail(TEXT("Open Lv3_Ingame first."));
    const TSet<FString> Expected = {TEXT("/Game/Lv3_Ingame/Lv3_Ingame"),
        TEXT("/Game/Lv3_Ingame/SubLevels/Ingame_Env"), TEXT("/Game/Lv3_Ingame/SubLevels/Ingame_Gameplay")};
    if (World->GetLevels().Num() != 3 || World->GetStreamingLevels().Num() != 2)
        return Fail(TEXT("Load and show Ingame_Env and Ingame_Gameplay before exporting."));
    for (ULevel* Level : World->GetLevels())
        if (!Expected.Contains(Level->GetOutermost()->GetName()) || !FLevelUtils::IsLevelVisible(Level))
            return Fail(TEXT("Unexpected or hidden sublevel; export cancelled."));
    for (ULevelStreaming* Streaming : World->GetStreamingLevels())
        if (!Streaming || !Streaming->GetLoadedLevel() || !Streaming->LevelTransform.Equals(FTransform::Identity))
            return Fail(TEXT("All streaming levels must be loaded with identity transforms."));

    for (const auto& Entry : UNetSettings::Get()->NpcClasses)
        if (!Entry.Value.IsNull()) Types.Add(MakeShared<FJsonValueNumber>(Entry.Key));
    TArray<AMonsterSpawnRegion*> Actors;
    for (TActorIterator<AMonsterSpawnRegion> It(World); It; ++It) Actors.Add(*It);
    Actors.Sort([](const AMonsterSpawnRegion& A, const AMonsterSpawnRegion& B) { return A.GetPathName() < B.GetPathName(); });
    for (const AMonsterSpawnRegion* Actor : Actors)
    {
        if (!Actor->bEnabled) continue;
        const FTransform Transform = Actor->GetActorTransform();
        const FRotator Rotation = Transform.Rotator();
        if (Transform.ContainsNaN() || !Transform.GetScale3D().Equals(FVector::OneVector, 0.0001)
            || !FMath::IsNearlyZero(Rotation.Pitch, 0.001) || !FMath::IsNearlyZero(Rotation.Roll, 0.001)
            || !FMath::IsFinite(Actor->Radius))
        {
            Errors.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel() + TEXT(": use Radius, not actor scale or tilt.")));
            continue;
        }
        const auto Region = MakeShared<FJsonObject>();
        Region->SetStringField(TEXT("actor"), Actor->GetPathName());
        Region->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Region->SetNumberField(TEXT("x"), Transform.GetLocation().X);
        Region->SetNumberField(TEXT("y"), Transform.GetLocation().Y);
        Region->SetNumberField(TEXT("radius"), Actor->Radius);
        TArray<TSharedPtr<FJsonValue>> Entries;
        for (const FMonsterSpawnEntry& Entry : Actor->Monsters)
        {
            if (!FMath::IsFinite(Entry.RespawnSeconds))
            {
                Errors.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel() + TEXT(": invalid respawn delay.")));
                continue;
            }
            const auto Row = MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("type"), Entry.MonsterType);
            Row->SetNumberField(TEXT("count"), Entry.Count);
            Row->SetNumberField(TEXT("respawnSeconds"), Entry.RespawnSeconds);
            Entries.Add(MakeShared<FJsonValueObject>(Row));
        }
        Region->SetArrayField(TEXT("monsters"), Entries);
        Regions.Add(MakeShared<FJsonValueObject>(Region));
    }
    return Finish();
}
