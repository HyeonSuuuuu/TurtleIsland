#include "CollisionExportLibrary.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FString UCollisionExportLibrary::AuditCollisionBehavior()
{
    if (!IsCollisionExportAllowed()) return TEXT("{}");
    UWorld* World = GEditor->GetEditorWorldContext().World();
    using FValues = TArray<TSharedPtr<FJsonValue>>;
    FValues Actors, Blueprints;
    TSet<UBlueprint*> Seen;
    const auto AddClass = [&Seen](UClass* Class)
    {
        for (; Class; Class = Class->GetSuperClass())
            if (UBlueprint* BP = Cast<UBlueprint>(Class->ClassGeneratedBy)) Seen.Add(BP);
    };
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        AddClass(Actor->GetClass());
        FValues Components;
        TInlineComponentArray<UActorComponent*> All;
        Actor->GetComponents(All);
        for (UActorComponent* Component : All)
        {
            AddClass(Component->GetClass());
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("path"), Component->GetPathName());
            Row->SetStringField(TEXT("class"), Component->GetClass()->GetPathName());
            Row->SetBoolField(TEXT("tick"), Component->PrimaryComponentTick.bCanEverTick);
            if (auto* Primitive = Cast<UPrimitiveComponent>(Component))
            {
                Row->SetBoolField(TEXT("simulates"), Primitive->IsSimulatingPhysics());
                Row->SetBoolField(TEXT("movable"), Primitive->Mobility == EComponentMobility::Movable);
                Row->SetBoolField(TEXT("blocks_pawn"), Primitive->IsQueryCollisionEnabled() &&
                    Primitive->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
            }
            Components.Add(MakeShared<FJsonValueObject>(Row));
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("path"), Actor->GetPathName());
        Row->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Row->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
        Row->SetStringField(TEXT("parent"), Actor->GetAttachParentActor() ? Actor->GetAttachParentActor()->GetPathName() : TEXT(""));
        Row->SetArrayField(TEXT("components"), Components);
        Actors.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (ULevel* Level : World->GetLevels())
        if (UBlueprint* BP = Level->GetLevelScriptBlueprint(true)) Seen.Add(BP);
    for (UBlueprint* BP : Seen)
    {
        FValues Graphs;
        TArray<UEdGraph*> AllGraphs;
        BP->GetAllGraphs(AllGraphs);
        for (UEdGraph* Graph : AllGraphs)
        {
            FValues Nodes;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node) continue;
                FValues Pins;
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin) continue;
                    FValues Links;
                    for (UEdGraphPin* Link : Pin->LinkedTo)
                        if (Link) Links.Add(MakeShared<FJsonValueString>(Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString()));
                    auto P = MakeShared<FJsonObject>();
                    P->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    P->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                    P->SetStringField(TEXT("default"), Pin->DefaultValue);
                    P->SetStringField(TEXT("object"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT(""));
                    P->SetArrayField(TEXT("links"), Links);
                    Pins.Add(MakeShared<FJsonValueObject>(P));
                }
                auto N = MakeShared<FJsonObject>();
                N->SetStringField(TEXT("name"), Node->GetName());
                N->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
                N->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                N->SetArrayField(TEXT("pins"), Pins);
                Nodes.Add(MakeShared<FJsonValueObject>(N));
            }
            auto G = MakeShared<FJsonObject>();
            G->SetStringField(TEXT("name"), Graph->GetName());
            G->SetArrayField(TEXT("nodes"), Nodes);
            Graphs.Add(MakeShared<FJsonValueObject>(G));
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("path"), BP->GetPathName());
        Row->SetStringField(TEXT("parent"), BP->ParentClass ? BP->ParentClass->GetPathName() : TEXT(""));
        Row->SetNumberField(TEXT("timelines"), BP->Timelines.Num());
        Row->SetArrayField(TEXT("graphs"), Graphs);
        Blueprints.Add(MakeShared<FJsonValueObject>(Row));
    }
    auto Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("actors"), Actors);
    Root->SetArrayField(TEXT("blueprints"), Blueprints);
    FString Result;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
    return Result;
}
