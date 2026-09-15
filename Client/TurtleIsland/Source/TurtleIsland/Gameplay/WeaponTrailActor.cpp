#include "Gameplay/WeaponTrailActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

AWeaponTrailActor::AWeaponTrailActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after animation, so the blade pose is this frame's
	Ribbon = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ribbon"));
	Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ribbon->SetCastShadow(false);
	Ribbon->bUseAsyncCooking = false;
	SetRootComponent(Ribbon);
	SetCanBeDamaged(false);
}

void AWeaponTrailActor::Spawn(USkeletalMeshComponent* Mesh, FName Bone, const FVector& BaseOffset, const FVector& TipOffset,
	float Delay, float Duration, const FLinearColor& Colour)
{
	UWorld* World = Mesh ? Mesh->GetWorld() : nullptr;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/M_VFX_Additive.M_VFX_Additive"));
	if (!World || !Base || Mesh->GetBoneIndex(Bone) == INDEX_NONE) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWeaponTrailActor* Trail = World->SpawnActor<AWeaponTrailActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!Trail) return;
	Trail->Source = Mesh; Trail->Bone = Bone; Trail->BaseOffset = BaseOffset; Trail->TipOffset = TipOffset;
	Trail->Delay = Delay; Trail->Duration = Duration;
	Trail->Material = UMaterialInstanceDynamic::Create(Base, Trail);
	if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/VFX/Textures/T_VFX_WindTrail.T_VFX_WindTrail")))
		Trail->Material->SetTextureParameterValue(TEXT("Tex"), Texture);
	Trail->Material->SetVectorParameterValue(TEXT("Color"), Colour);
	Trail->Material->SetScalarParameterValue(TEXT("Opacity"), 1.f);
}

void AWeaponTrailActor::Sample()
{
	const USkeletalMeshComponent* Mesh = Source.Get();
	if (!Mesh) return;
	const FTransform BoneTransform = Mesh->GetBoneTransform(Mesh->GetBoneIndex(Bone));
	FSample Next{BoneTransform.TransformPosition(BaseOffset), BoneTransform.TransformPosition(TipOffset), Age};
	// Subdivide fast swings with a Catmull-Rom step so the arc stays round at low frame rates.
	if (Samples.Num() >= 2 && FVector::Dist(Samples.Last().Tip, Next.Tip) > 25.f)
	{
		const FSample P0 = Samples[Samples.Num() - 2], P1 = Samples.Last();
		for (int32 Step = 1; Step <= 2; ++Step)
		{
			const float T = Step / 3.f;
			// Catmull-Rom from P1 to Next; the unknown point after Next is extrapolated.
			auto Spline = [T](const FVector& A, const FVector& B, const FVector& C)
			{
				const FVector D = C * 2.f - B;
				return 0.5f * (B * 2.f + (C - A) * T + (A * 2.f - B * 5.f + C * 4.f - D) * T * T + (B * 3.f - A - C * 3.f + D) * T * T * T);
			};
			Samples.Add({Spline(P0.Base, P1.Base, Next.Base), Spline(P0.Tip, P1.Tip, Next.Tip), FMath::Lerp(P1.Time, Next.Time, T)});
		}
	}
	Samples.Add(Next);
}

void AWeaponTrailActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const bool bSampling = Age >= Delay && Age <= Delay + Duration && Source.IsValid();
	if (bSampling) Sample();
	Samples.RemoveAll([this](const FSample& S) { return Age - S.Time > FadeSeconds; });
	if (!bSampling && Age > Delay && Samples.IsEmpty()) { Destroy(); return; }
	Rebuild();
}

void AWeaponTrailActor::Rebuild()
{
	Ribbon->ClearAllMeshSections();
	if (Samples.Num() < 2) return;
	TArray<FVector> Vertices; TArray<int32> Triangles; TArray<FVector> Normals; TArray<FVector2D> UVs;
	TArray<FLinearColor> Colours; TArray<FProcMeshTangent> Tangents;
	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		const float U = FMath::Clamp((Age - Samples[i].Time) / FadeSeconds, 0.f, 1.f); // 0 = newest
		Vertices.Add(Samples[i].Base); Vertices.Add(Samples[i].Tip);
		UVs.Add({U, 0.f}); UVs.Add({U, 1.f});
		Normals.Add(FVector::UpVector); Normals.Add(FVector::UpVector);
		if (i == 0) continue;
		const int32 A = (i - 1) * 2, B = i * 2;
		Triangles.Append({A, B, A + 1, A + 1, B, B + 1});
	}
	Ribbon->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colours, Tangents, false);
	Ribbon->SetMaterial(0, Material);
}
