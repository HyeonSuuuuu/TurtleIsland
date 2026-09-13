#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/SplineComponent.h"
#include "World/MonsterSpawnRegion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterSpawnRegionTest, "TurtleIsland.Editor.MonsterSpawnRegion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonsterSpawnRegionTest::RunTest(const FString& Parameters)
{
    FTestWorldWrapper TestWorld;
    if (!TestWorld.CreateTestWorld(EWorldType::Editor)) return false;
    AMonsterSpawnRegion* Region = TestWorld.GetTestWorld()->SpawnActor<AMonsterSpawnRegion>();
    if (!TestNotNull(TEXT("Region created"), Region)) return false;
    TestTrue(TEXT("Excluded from cooked game"), Region->IsEditorOnly());
    TestTrue(TEXT("Hidden in PIE"), Region->IsHidden());
    TestFalse(TEXT("No actor collision"), Region->GetActorEnableCollision());
    TestFalse(TEXT("No runtime tick"), Region->PrimaryActorTick.bCanEverTick);
    USplineComponent* Preview = Region->FindComponentByClass<USplineComponent>();
    if (!TestNotNull(TEXT("Visible boundary"), Preview)) return false;
    TestTrue(TEXT("Visible when unselected"), Preview->bDrawDebug);
    TestEqual(TEXT("No collision"), Preview->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("No nav obstacle"), Preview->CanEverAffectNavigation());
    Region->Radius = 450.f;
    Region->OnConstruction(Region->GetActorTransform());
    TestTrue(TEXT("Closed circular boundary"), Preview->IsClosedLoop());
    for (int32 Index = 0; Index < Preview->GetNumberOfSplinePoints(); ++Index)
    {
        const FVector Point = Preview->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
        TestTrue(TEXT("Preview follows configured radius"), FMath::IsNearlyEqual(Point.Size2D(), 450.0, 0.001));
        TestEqual(TEXT("XY plane only"), Point.Z, 0.0);
    }
    TestEqual(TEXT("Default one monster entry"), Region->Monsters.Num(), 1);
    TestEqual(TEXT("Default respawn delay"), Region->Monsters[0].RespawnSeconds, 10.0);
    return true;
}

#endif
