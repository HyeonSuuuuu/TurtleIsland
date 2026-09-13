#include "CollisionExportLibrary.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "DrawDebugHelpers.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"
#include "NavModifierVolume.h"
#include "NavAreas/NavArea_Null.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, TurtleIslandCollisionEditor)

namespace
{
using FJson = TSharedPtr<FJsonObject>;
using FValues = TArray<TSharedPtr<FJsonValue>>;

TSharedPtr<FJsonValue> Value(const FJson& Object) { return MakeShared<FJsonValueObject>(Object); }
TSharedPtr<FJsonValue> Number(double V) { return MakeShared<FJsonValueNumber>(V); }
TSharedPtr<FJsonValue> Text(const FString& V) { return MakeShared<FJsonValueString>(V); }
TSharedPtr<FJsonValue> XY(FVector2D V) { return MakeShared<FJsonValueArray>(FValues{Number(V.X), Number(V.Y)}); }

bool Tagged(const UPrimitiveComponent* Component, const FName Tag)
{
    return Component->ComponentHasTag(Tag) || Component->GetOwner()->ActorHasTag(Tag);
}

double Cross(FVector2D A, FVector2D B, FVector2D C)
{
    return (B.X-A.X)*(C.Y-A.Y) - (B.Y-A.Y)*(C.X-A.X);
}

// Projected convex bodies can have more than eight vertices, especially when tilted.
TArray<FVector2D> Hull(TArray<FVector2D> Points)
{
    Points.Sort([](const auto& A, const auto& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
    TArray<FVector2D> Unique;
    for (const auto& P : Points)
        if (Unique.IsEmpty() || !P.Equals(Unique.Last(), 0.001)) Unique.Add(P);
    if (Unique.Num() < 3) return {};
    TArray<FVector2D> Result;
    for (const auto& P : Unique)
    {
        while (Result.Num() >= 2 && Cross(Result[Result.Num()-2], Result.Last(), P) <= 0.0001) Result.Pop();
        Result.Add(P);
    }
    const int32 Lower = Result.Num();
    for (int32 I = Unique.Num()-2; I >= 0; --I)
    {
        while (Result.Num() > Lower && Cross(Result[Result.Num()-2], Result.Last(), Unique[I]) <= 0.0001) Result.Pop();
        Result.Add(Unique[I]);
    }
    Result.Pop();
    return Result;
}

struct FSceneBuilder
{
    UWorld* World;
    FVector2D Origin;
    int32 Cols, Rows;
    double Cell, GroundZ, HeightRange;
    FValues Polygons, Errors, Audit;
    TSet<FString> ReportedErrors;
    TArray<UPrimitiveComponent*> Grounds;
    TArray<TArray<int32>> GroundBuckets;
    TArray<UWaterBodyComponent*> Oceans;
    TArray<double> Heights;
    TArray<FString> Terrain;
    int32 FreeCells = 0;

    void Error(const FString& Source, const FString& Reason)
    {
        const FString Message=Source + TEXT(": ") + Reason;
        if (!ReportedErrors.Contains(Message)) { ReportedErrors.Add(Message); Errors.Add(Text(Message)); }
    }

    bool InBounds(const FBox& B) const
    {
        return B.Max.X >= Origin.X && B.Max.Y >= Origin.Y &&
            B.Min.X < Origin.X + Cols*Cell && B.Min.Y < Origin.Y + Rows*Cell;
    }

    int32 Index(double X, double Y) const
    {
        const int32 C = FMath::FloorToInt((X-Origin.X)/Cell), R = FMath::FloorToInt((Y-Origin.Y)/Cell);
        return C >= 0 && C < Cols && R >= 0 && R < Rows ? R*Cols+C : INDEX_NONE;
    }

    void AddGround(UPrimitiveComponent* Component)
    {
        const int32 Id = Grounds.Add(Component);
        const FBox B = Component->Bounds.GetBox();
        const int32 C0 = FMath::Clamp(FMath::FloorToInt((B.Min.X-Origin.X)/Cell), 0, Cols-1);
        const int32 C1 = FMath::Clamp(FMath::FloorToInt((B.Max.X-Origin.X)/Cell), 0, Cols-1);
        const int32 R0 = FMath::Clamp(FMath::FloorToInt((B.Min.Y-Origin.Y)/Cell), 0, Rows-1);
        const int32 R1 = FMath::Clamp(FMath::FloorToInt((B.Max.Y-Origin.Y)/Cell), 0, Rows-1);
        for (int32 R=R0; R<=R1; ++R) for (int32 C=C0; C<=C1; ++C) GroundBuckets[R*Cols+C].Add(Id);
    }

    bool SampleGround(double X, double Y, double& Z) const
    {
        const int32 Id = Index(X,Y);
        if (Id == INDEX_NONE) return false;
        bool HitGround = false;
        Z = -DBL_MAX;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(ServerGroundExport), true);
        for (int32 GroundId : GroundBuckets[Id])
        {
            FHitResult Hit;
            if (Grounds[GroundId]->LineTraceComponent(Hit, FVector(X,Y,GroundZ+HeightRange),
                FVector(X,Y,GroundZ-HeightRange), Params) && Hit.ImpactNormal.Z >= 0.707107 && Hit.ImpactPoint.Z > Z)
            {
                Z = Hit.ImpactPoint.Z;
                HitGround = true;
            }
        }
        if (!HitGround) return false;
        for (const auto* Ocean : Oceans)
        {
            const auto Query = Ocean->TryQueryWaterInfoClosestToWorldLocation(FVector(X,Y,Z),
                EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeImmersionDepth);
            if (Query.HasError()) return false;
            if (!Query.GetValue().IsInExclusionVolume() && Query.GetValue().IsInWater()) return false;
        }
        return true;
    }

    void BuildGround()
    {
        Heights.Init(GroundZ, Cols*Rows);
        const FVector2D Offsets[] = {{0.5,0.5},{0.02,0.02},{0.98,0.02},{0.02,0.98},{0.98,0.98}};
        for (int32 R=0; R<Rows; ++R)
        {
            FString Row;
            Row.Reserve(Cols);
            for (int32 C=0; C<Cols; ++C)
            {
                double Low = DBL_MAX, High = -DBL_MAX;
                bool Valid = true;
                for (const auto& Offset : Offsets)
                {
                    double Z;
                    if (!SampleGround(Origin.X+(C+Offset.X)*Cell, Origin.Y+(R+Offset.Y)*Cell, Z)) { Valid=false; break; }
                    Low=FMath::Min(Low,Z); High=FMath::Max(High,Z);
                }
                Valid &= High-Low <= 45.;
                if (Valid) { Heights[R*Cols+C]=(Low+High)*0.5; ++FreeCells; }
                Row.AppendChar(Valid ? '0' : '1');
            }
            Terrain.Add(MoveTemp(Row));
        }
        if (!FreeCells) Error(TEXT("terrain"), TEXT("No dry walkable ground was sampled"));
    }

    void AddPolygon(const TArray<FVector2D>& Points, const FString& Source, double Z)
    {
        if (Points.Num() < 3) { Error(Source, TEXT("Degenerate projected collider")); return; }
        auto Emit = [&](const FValues& Vertices)
        {
            FJson Shape = MakeShared<FJsonObject>();
            Shape->SetStringField(TEXT("source"), Source);
            Shape->SetArrayField(TEXT("vertices"), Vertices);
            Shape->SetNumberField(TEXT("preview_z"), Z);
            Polygons.Add(Value(Shape));
        };
        if (Points.Num() <= 8)
        {
            FValues Vertices; for (const auto& P : Points) Vertices.Add(XY(P));
            Emit(Vertices);
        }
        else
        {
            // Interior-centred triangles avoid nearly collinear fan tips on small curves.
            FVector2D Center=FVector2D::ZeroVector;
            for (const auto& P : Points) Center+=P;
            Center/=Points.Num();
            for (int32 I=0; I<Points.Num(); ++I)
                Emit(FValues{XY(Center),XY(Points[I]),XY(Points[(I+1)%Points.Num()])});
        }
    }

    void ProjectBody(const TArray<FVector>& Vertices, const FString& Source)
    {
        if (Vertices.Num()<4 || Vertices.Num()>1024) { Error(Source,TEXT("Unsupported convex vertex count")); return; }
        FBox B(ForceInit);
        for (const auto& P : Vertices)
        {
            if (P.ContainsNaN()) { Error(Source,TEXT("Non-finite body transform")); return; }
            B += P;
        }
        if (!InBounds(B)) return;
        double Z;
        if (!SampleGround(B.GetCenter().X,B.GetCenter().Y,Z)) Z=GroundZ;
        // Single ground layer only. Project the portion intersecting the standing body band.
        const double Low=Z+45., High=Z+192.;
        if (B.Max.Z <= Low || B.Min.Z >= High) return;
        TArray<FVector2D> Points;
        for (const auto& P : Vertices) if (P.Z>=Low && P.Z<=High) Points.Add(FVector2D(P));
        for (int32 I=0; I<Vertices.Num(); ++I) for (int32 J=I+1; J<Vertices.Num(); ++J)
            for (double Plane : {Low,High})
            {
                const auto& A=Vertices[I]; const auto& C=Vertices[J];
                if ((A.Z<Plane && C.Z>Plane) || (C.Z<Plane && A.Z>Plane))
                    Points.Add(FVector2D(FMath::Lerp(A,C,(Plane-A.Z)/(C.Z-A.Z))));
            }
        AddPolygon(Hull(MoveTemp(Points)),Source,Z+50.);
    }

    void ProjectCapsule(FVector A, FVector B, double Radius, const FString& Source)
    {
        FBox Bounds(A,B); Bounds=Bounds.ExpandBy(Radius);
        if (!InBounds(Bounds)) return;
        double Z;
        if (!SampleGround((A.X+B.X)*0.5,(A.Y+B.Y)*0.5,Z)) Z=GroundZ;
        if (Bounds.Max.Z<=Z+45. || Bounds.Min.Z>=Z+192.) return;
        // Circumscribed 32-gon, maximum radial excess 0.49%; never shrinks collision.
        TArray<FVector2D> Points;
        for (const FVector End : {A,B}) for (int32 I=0; I<32; ++I)
        {
            const double Angle=2.*PI*I/32.;
            Points.Add(FVector2D(End)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*(Radius/FMath::Cos(PI/32.)));
        }
        AddPolygon(Hull(MoveTemp(Points)),Source,Z+50.);
    }

    void Extract(UPrimitiveComponent* Component, const FTransform& Transform, const FString& Source)
    {
        if (Transform.ContainsNaN() || Transform.GetScale3D().GetAbsMin() < 0.0001)
        { Error(Source,TEXT("Invalid or zero-scale body transform")); return; }
        UBodySetup* Body=Component->GetBodySetup();
        if (!Body) { Error(Source,TEXT("Blocking component has no supported BodySetup")); return; }
        if (Body->GetCollisionTraceFlag()==CTF_UseComplexAsSimple)
        { Error(Source,TEXT("Complex-as-simple: add simple convex collision or a reviewed replacement")); return; }
        if (Component->IsSimulatingPhysics() ||
            (Component->Mobility==EComponentMobility::Movable && !Tagged(Component,TEXT("ServerCollisionStatic"))))
            Error(Component->GetPathName(),TEXT("Movable body: review as ServerCollisionStatic or provide dynamic collision; candidate preview only"));
        if (Cast<USplineMeshComponent>(Component))
        { Error(Source,TEXT("Deformed spline collision requires a reviewed static replacement")); return; }
        const auto& Geometry=Body->AggGeom;
        FTransform Rigid=Transform; Rigid.SetScale3D(FVector::OneVector);
        const FVector Scale=Transform.GetScale3D();
        for (const auto& Box : Geometry.BoxElems)
        {
            if (!CollisionEnabledHasQuery(Box.GetCollisionEnabled())) continue;
            const auto Scaled=Box.GetFinalScaled(Scale,FTransform::Identity);
            const FTransform Pose=Scaled.GetTransform()*Rigid;
            TArray<FVector> Points;
            for (int32 I=0; I<8; ++I) Points.Add(Pose.TransformPosition(FVector(
                (I&1 ? 0.5 : -0.5)*Scaled.X,(I&2 ? 0.5 : -0.5)*Scaled.Y,(I&4 ? 0.5 : -0.5)*Scaled.Z)));
            ProjectBody(Points,Source);
        }
        for (const auto& Convex : Geometry.ConvexElems)
        {
            if (!CollisionEnabledHasQuery(Convex.GetCollisionEnabled())) continue;
            TArray<FVector> Points;
            for (const auto& P : Convex.VertexData) Points.Add(Transform.TransformPosition(Convex.GetTransform().TransformPosition(P)));
            ProjectBody(Points,Source);
        }
        for (const auto& Sphere : Geometry.SphereElems)
        {
            if (!CollisionEnabledHasQuery(Sphere.GetCollisionEnabled())) continue;
            const auto Scaled=Sphere.GetFinalScaled(Scale,FTransform::Identity);
            const FVector Center=Rigid.TransformPosition(Scaled.Center);
            ProjectCapsule(Center,Center,Scaled.Radius,Source);
        }
        for (const auto& Capsule : Geometry.SphylElems)
        {
            if (!CollisionEnabledHasQuery(Capsule.GetCollisionEnabled())) continue;
            const auto Scaled=Capsule.GetFinalScaled(Scale,FTransform::Identity);
            const FTransform Pose=Scaled.GetTransform()*Rigid;
            ProjectCapsule(Pose.TransformPosition(FVector(0,0,-Scaled.Length*0.5)),
                Pose.TransformPosition(FVector(0,0,Scaled.Length*0.5)),Scaled.Radius,Source);
        }
        const int32 Supported=Geometry.BoxElems.Num()+Geometry.ConvexElems.Num()+Geometry.SphereElems.Num()+Geometry.SphylElems.Num();
        if (Geometry.GetElementCount()!=Supported) Error(Source,TEXT("Unsupported aggregate geometry element"));
    }
};
}

bool UCollisionExportLibrary::IsCollisionExportAllowed()
{
    return GEditor && !GEditor->PlayWorld && GEditor->GetEditorWorldContext().World();
}

FString UCollisionExportLibrary::GetCollisionLevelPath()
{
    return IsCollisionExportAllowed() ? GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName() : FString();
}

FString UCollisionExportLibrary::BuildCollisionScene(FVector SpawnLocation, float SpawnYaw, float GroundZ,
    FVector2D Origin, int32 Cols, int32 Rows, float CellSize, float HeightRange)
{
    UWorld* World=GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || GEditor->PlayWorld || SpawnLocation.ContainsNaN() || Origin.ContainsNaN() ||
        !FMath::IsFinite(GroundZ) || !FMath::IsFinite(SpawnYaw) || !FMath::IsFinite(CellSize) ||
        !FMath::IsFinite(HeightRange) || Cols<=0 || Rows<=0 || Cols>4096 || Rows>4096 ||
        int64(Cols)*Rows>1000000 || CellSize<25 || CellSize>200 || HeightRange<=0 || HeightRange>1000)
        return TEXT("{\"complete\":false,\"errors\":[\"Invalid world or ground grid parameters\"]}");
    FSceneBuilder Scene{World,Origin,Cols,Rows,CellSize,GroundZ,HeightRange};
    Scene.GroundBuckets.SetNum(Cols*Rows);
    TArray<UPrimitiveComponent*> Blockers;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor=*It;
        if (const auto* Modifier=Cast<ANavModifierVolume>(Actor))
            if (Modifier->GetAreaClass() && Modifier->GetAreaClass()->IsChildOf(UNavArea_Null::StaticClass()) &&
                Scene.InBounds(Modifier->GetNavigationBounds()))
                Scene.Error(Actor->GetPathName(),TEXT("NavArea_Null restriction requires an explicit shape/ground exclusion; old NavMesh mask is not reused"));
        if (Actor->IsEditorOnly() || Actor->IsA<APawn>()) continue;
        if (auto* Water=Cast<AWaterBody>(Actor))
        {
            if (Water->GetWaterBodyType()==EWaterBodyType::Ocean) Scene.Oceans.Add(Water->GetWaterBodyComponent());
            else Scene.Error(Actor->GetPathName(),TEXT("Only the ocean ground layer is supported; river/lake needs an explicit no-walk mask"));
        }
        TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
        for (auto* Component : Components)
        {
            if (!Component->IsRegistered() || !Component->IsQueryCollisionEnabled() ||
                Component->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block) continue;
            const FBox B=Component->Bounds.GetBox();
            if (!Scene.InBounds(B) || B.Max.Z<GroundZ-HeightRange || B.Min.Z>GroundZ+HeightRange+192.) continue;
            FJson Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("source"),Component->GetPathName());
            Row->SetStringField(TEXT("actor"),Actor->GetActorLabel());
            Row->SetStringField(TEXT("class"),Component->GetClass()->GetName());
            Row->SetStringField(TEXT("role"),TEXT("blocker"));
            if (Tagged(Component,TEXT("ServerCollisionIgnore"))) Row->SetStringField(TEXT("role"),TEXT("explicit_ignore"));
            else if (Component->IsA<ULandscapeHeightfieldCollisionComponent>() || Tagged(Component,TEXT("ServerCollisionGround")))
            {
                Row->SetStringField(TEXT("role"),TEXT("ground")); Scene.AddGround(Component);
                if (Component->IsSimulatingPhysics() ||
                    (Component->Mobility==EComponentMobility::Movable && !Tagged(Component,TEXT("ServerCollisionStatic"))))
                    Scene.Error(Component->GetPathName(),TEXT("Moving ground cannot be frozen without static classification"));
            }
            else if (!Component->GetBodySetup() &&
                (!Component->GetBodyInstance() || !Component->GetBodyInstance()->IsValidBodyInstance()))
                Row->SetStringField(TEXT("role"),TEXT("no_physics_body"));
            else
            {
                if (Component->Mobility==EComponentMobility::Movable)
                    Row->SetStringField(TEXT("role"),Tagged(Component,TEXT("ServerCollisionStatic")) ? TEXT("reviewed_static") : TEXT("unreviewed_movable"));
                Blockers.Add(Component);
            }
            if (const auto* Mesh=Cast<UStaticMeshComponent>(Component))
                Row->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh->GetStaticMesh()));
            Scene.Audit.Add(Value(Row));
        }
    }
    Scene.BuildGround();
    for (auto* Component : Blockers)
    {
        if (auto* Instances=Cast<UInstancedStaticMeshComponent>(Component))
        {
            for (int32 I=0; I<Instances->GetInstanceCount(); ++I)
            {
                FTransform Transform;
                if (Instances->GetInstanceTransform(I,Transform,true))
                    Scene.Extract(Component,Transform,Component->GetPathName()+FString::Printf(TEXT("[%d]"),I));
                else Scene.Error(Component->GetPathName(),TEXT("Could not read instance transform"));
            }
        }
        else Scene.Extract(Component,Component->GetComponentTransform(),Component->GetPathName());
    }
    FJson Root=MakeShared<FJsonObject>(), Terrain=MakeShared<FJsonObject>(), Spawn=MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema"),1); Root->SetStringField(TEXT("units"),TEXT("cm"));
    Root->SetStringField(TEXT("source_level"),World->GetOutermost()->GetName());
    Root->SetStringField(TEXT("exported_utc"),FDateTime::UtcNow().ToIso8601());
    Spawn->SetNumberField(TEXT("x"),SpawnLocation.X); Spawn->SetNumberField(TEXT("y"),SpawnLocation.Y);
    Spawn->SetNumberField(TEXT("z"),SpawnLocation.Z); Spawn->SetNumberField(TEXT("yaw"),SpawnYaw);
    Spawn->SetNumberField(TEXT("ground_z"),GroundZ); Root->SetObjectField(TEXT("spawn"),Spawn);
    Terrain->SetNumberField(TEXT("cols"),Cols); Terrain->SetNumberField(TEXT("rows"),Rows);
    Terrain->SetNumberField(TEXT("cell"),CellSize); Terrain->SetNumberField(TEXT("height_range"),HeightRange);
    Terrain->SetArrayField(TEXT("origin"),FValues{Number(Origin.X),Number(Origin.Y)});
    FValues RowsJson; for (const auto& Row : Scene.Terrain) RowsJson.Add(Text(Row));
    Terrain->SetArrayField(TEXT("blocked"),RowsJson); Terrain->SetNumberField(TEXT("free_cells"),Scene.FreeCells);
    Root->SetObjectField(TEXT("terrain"),Terrain);
    Root->SetArrayField(TEXT("polygons"),Scene.Polygons); Root->SetArrayField(TEXT("audit"),Scene.Audit);
    Root->SetArrayField(TEXT("errors"),Scene.Errors); Root->SetBoolField(TEXT("complete"),Scene.Errors.IsEmpty());
    FString Result; FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Result));
    return Result;
}

void UCollisionExportLibrary::PreviewCollisionScene(const FString& SceneJson, float Duration)
{
    UWorld* World=GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    FJson Root;
    if (!World || GEditor->PlayWorld || !FMath::IsFinite(Duration) || Duration<=0 || Duration>120 ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SceneJson),Root) || !Root.IsValid()) return;
    const FValues* Shapes=nullptr;
    if (!Root->TryGetArrayField(TEXT("polygons"),Shapes)) return;
    for (const auto& Shape : *Shapes)
    {
        if (Shape->Type!=EJson::Object) continue;
        const auto& Object=Shape->AsObject();
        const FValues* FoundVertices=nullptr;
        double Z;
        if (!Object->TryGetArrayField(TEXT("vertices"),FoundVertices) ||
            !Object->TryGetNumberField(TEXT("preview_z"),Z) || !FMath::IsFinite(Z)) continue;
        const auto& Vertices=*FoundVertices;
        if (Vertices.Num()<3 || Vertices.Num()>8) continue;
        bool Valid=true;
        for (const auto& P : Vertices)
        {
            const FValues* Coords=nullptr;
            if (!P->TryGetArray(Coords) || Coords->Num()!=2) { Valid=false; break; }
            for (const auto& C : *Coords)
                if (C->Type!=EJson::Number || !FMath::IsFinite(C->AsNumber())) Valid=false;
        }
        if (!Valid) continue;
        for (int32 I=0; I<Vertices.Num(); ++I)
        {
            const auto& A=Vertices[I]->AsArray(); const auto& B=Vertices[(I+1)%Vertices.Num()]->AsArray();
            DrawDebugLine(World,FVector(A[0]->AsNumber(),A[1]->AsNumber(),Z),
                FVector(B[0]->AsNumber(),B[1]->AsNumber(),Z),FColor::Cyan,false,Duration,0,2.f);
        }
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShapeProjectionTest,"TurtleIsland.Collision.ShapeProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShapeProjectionTest::RunTest(const FString& Parameters)
{
    FSceneBuilder Scene{nullptr,FVector2D(-500,-500),10,10,100,0,200};
    Scene.GroundBuckets.SetNum(100);
    TArray<FVector> Fence;
    for (int32 I=0; I<8; ++I) Fence.Add(FVector(I&1 ? 5 : -5,I&2 ? 200 : -200,I&4 ? 150 : 0));
    Scene.ProjectBody(Fence,TEXT("synthetic_fence"));
    TestEqual(TEXT("Thin fence remains one polygon"),Scene.Polygons.Num(),1);
    if (Scene.Polygons.IsEmpty()) return false;
    const auto& V=Scene.Polygons[0]->AsObject()->GetArrayField(TEXT("vertices"));
    TestEqual(TEXT("Projected box has four vertices"),V.Num(),4);
    for (const auto& P : V) TestEqual(TEXT("No grid or radius expansion during export"),FMath::Abs(P->AsArray()[0]->AsNumber()),5.);
    auto Floor=Fence; for (auto& P : Floor) P.Z=P.Z>0 ? 0 : -10;
    Scene.ProjectBody(Floor,TEXT("floor"));
    auto Roof=Fence; for (auto& P : Roof) P.Z+=300;
    Scene.ProjectBody(Roof,TEXT("roof"));
    TestEqual(TEXT("Floor and overhead roof do not become XY blockers"),Scene.Polygons.Num(),1);
    TArray<FVector2D> Cloud{{0,0},{100,0},{100,100},{0,100},{50,50},{0,0}};
    TestEqual(TEXT("Projection hull removes duplicate and interior vertices"),Hull(Cloud).Num(),4);
    Scene.ProjectCapsule(FVector(0,0,100),FVector(50,0,100),20,TEXT("capsule"));

    FJson Root=MakeShared<FJsonObject>();
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(
        "{\"schema\":1,\"units\":\"cm\",\"complete\":true,\"errors\":[],\"source_level\":\"/Test/UnrealExport\","
        "\"spawn\":{\"x\":-250,\"y\":-250,\"z\":96,\"yaw\":0,\"ground_z\":0},"
        "\"terrain\":{\"cols\":10,\"rows\":10,\"cell\":100,\"origin\":[-500,-500],\"blocked\":[]}}"))),Root);
    FValues Rows; for (int I=0; I<10; ++I) Rows.Add(Text(TEXT("0000000000")));
    Root->GetObjectField(TEXT("terrain"))->SetArrayField(TEXT("blocked"),Rows);
    Root->SetArrayField(TEXT("polygons"),Scene.Polygons);
    FString Json; FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("Save native export fixture for server round-trip test"),FFileHelper::SaveStringToFile(Json,
        *(FPaths::ProjectSavedDir()/TEXT("ShapeExportFixture.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("Curved geometry is decomposed into convex parts"),Scene.Polygons.Num()>1);
    for (const auto& P : Scene.Polygons)
        TestTrue(TEXT("Each exported part fits Box2D vertex limit"),P->AsObject()->GetArrayField(TEXT("vertices")).Num()<=8);
    TestTrue(TEXT("Projection has no errors"),Scene.Errors.IsEmpty());
    return true;
}
#endif
