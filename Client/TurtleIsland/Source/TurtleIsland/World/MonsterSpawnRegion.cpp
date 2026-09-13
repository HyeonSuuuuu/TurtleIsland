#include "World/MonsterSpawnRegion.h"

#include "Components/SceneComponent.h"
#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "Components/SplineComponent.h"
#endif

AMonsterSpawnRegion::AMonsterSpawnRegion()
{
    PrimaryActorTick.bCanEverTick = false;
    bIsEditorOnlyActor = true;
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent->SetMobility(EComponentMobility::Static);
    Monsters.AddDefaulted();
#if WITH_EDITORONLY_DATA
    Preview = CreateEditorOnlyDefaultSubobject<USplineComponent>(TEXT("SpawnBoundary"));
    Preview->SetupAttachment(RootComponent);
    Preview->SetMobility(EComponentMobility::Static);
    Preview->SetIsVisualizationComponent(true);
    Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Preview->SetGenerateOverlapEvents(false);
    Preview->SetCanEverAffectNavigation(false);
    Preview->SetDrawDebug(true);
    Preview->SetHiddenInGame(true);
    Icon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("RegionIcon"));
    Icon->SetupAttachment(RootComponent);
    Icon->SetIsVisualizationComponent(true);
    Icon->SetHiddenInGame(true);
    Icon->SetCanEverAffectNavigation(false);
#endif
    RefreshPreview();
}

void AMonsterSpawnRegion::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshPreview();
}

#if WITH_EDITOR
void AMonsterSpawnRegion::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    RefreshPreview();
}
#endif

void AMonsterSpawnRegion::RefreshPreview()
{
#if WITH_EDITORONLY_DATA
    if (Preview)
    {
        const float SafeRadius = FMath::IsFinite(Radius) ? FMath::Clamp(Radius, 1.f, 1000000.f) : 1.f;
        TArray<FVector> Points;
        constexpr int32 Segments = 64;
        for (int32 Index = 0; Index < Segments; ++Index)
        {
            const double Angle = 2.0 * PI * Index / Segments;
            Points.Emplace(SafeRadius * FMath::Cos(Angle), SafeRadius * FMath::Sin(Angle), 0);
        }
        Preview->SetSplinePoints(Points, ESplineCoordinateSpace::Local, false);
        for (int32 Index = 0; Index < Segments; ++Index)
            Preview->SetSplinePointType(Index, ESplinePointType::Linear, false);
        Preview->SetClosedLoop(true);
        const FLinearColor Color = bEnabled ? FLinearColor(0.05f, 1.f, 0.2f) : FLinearColor(0.3f, 0.3f, 0.3f);
        Preview->SetUnselectedSplineSegmentColor(Color);
        Preview->SetSelectedSplineSegmentColor(Color);
        Preview->MarkRenderStateDirty();
    }
#endif
}
