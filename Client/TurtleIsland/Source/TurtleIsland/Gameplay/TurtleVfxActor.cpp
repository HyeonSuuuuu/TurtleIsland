#include "Gameplay/TurtleVfxActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	enum class EFace : uint8 { Ground, Billboard };

	// Offset is actor-local (X forward, Z up, cm). Sizes are the plane's width in cm; Aspect stretches billboards upward.
	struct FLayerDef
	{
		ETurtleVfx Kind;
		const TCHAR* Texture;
		FLinearColor Color;
		EFace Face;
		FVector Offset;
		float Delay, Life, StartSize, EndSize;
		float Aspect = 1.f, Spin = 0.f, Rise = 0.f, YawOffset = 0.f;
	};

	const FLinearColor Cyan(0.55f * 3, 0.85f * 3, 3.f), Gold(4.f, 2.8f, 1.f), Purple(3.f, 1.4f, 4.f), Orange(5.f, 2.7f, 0.7f),
		Red(5.f, 1.f, 0.7f), Green(1.2f, 4.f, 1.8f), Blue(1.2f, 2.4f, 4.f), DarkRed(2.f, 0.25f, 0.25f), White(3.f, 3.2f, 3.5f);

	const FLayerDef LayerDefs[] = {

		{ETurtleVfx::Hit, TEXT("T_VFX_Burst"), Orange, EFace::Billboard, {0, 0, 20}, 0, 0.22f, 50, 150},
		{ETurtleVfx::Hit, TEXT("T_VFX_Star"), Orange * 0.8f, EFace::Billboard, {0, 0, 20}, 0, 0.28f, 40, 110, 1, 300},
		{ETurtleVfx::HitPlayer, TEXT("T_VFX_Burst"), Red, EFace::Billboard, {0, 0, 20}, 0, 0.22f, 50, 150},
		{ETurtleVfx::HitPlayer, TEXT("T_VFX_Star"), Red * 0.8f, EFace::Billboard, {0, 0, 20}, 0, 0.28f, 40, 110, 1, 300},

		{ETurtleVfx::LevelUp, TEXT("T_VFX_Ring"), Gold, EFace::Ground, {0, 0, -90}, 0, 0.9f, 80, 360},
		{ETurtleVfx::LevelUp, TEXT("T_VFX_Ring"), Gold, EFace::Ground, {0, 0, -90}, 0.3f, 0.9f, 80, 360},
		{ETurtleVfx::LevelUp, TEXT("T_VFX_Glow"), Gold * 0.4f, EFace::Billboard, {0, 0, 40}, 0, 1.1f, 160, 210, 2.6f},
		{ETurtleVfx::LevelUp, TEXT("T_VFX_Star"), Gold, EFace::Billboard, {0, 0, 60}, 0.1f, 1.0f, 60, 120, 1, 180, 120},

		{ETurtleVfx::PotionHP, TEXT("T_VFX_Ring"), Green, EFace::Ground, {0, 0, -90}, 0, 0.6f, 70, 220},
		{ETurtleVfx::PotionHP, TEXT("T_VFX_Glow"), Green * 0.4f, EFace::Billboard, {0, 0, 0}, 0, 0.7f, 120, 180, 1, 0, 80},
		{ETurtleVfx::PotionHP, TEXT("T_VFX_Star"), Green, EFace::Billboard, {30, 20, 40}, 0.15f, 0.6f, 40, 80, 1, 0, 140},
		{ETurtleVfx::PotionMP, TEXT("T_VFX_Ring"), Blue, EFace::Ground, {0, 0, -90}, 0, 0.6f, 70, 220},
		{ETurtleVfx::PotionMP, TEXT("T_VFX_Glow"), Blue * 0.4f, EFace::Billboard, {0, 0, 0}, 0, 0.7f, 120, 180, 1, 0, 80},
		{ETurtleVfx::PotionMP, TEXT("T_VFX_Star"), Blue, EFace::Billboard, {30, 20, 40}, 0.15f, 0.6f, 40, 80, 1, 0, 140},

		{ETurtleVfx::Death, TEXT("T_VFX_Ring"), DarkRed, EFace::Ground, {0, 0, -90}, 0, 0.9f, 60, 320},
		{ETurtleVfx::Death, TEXT("T_VFX_Burst"), DarkRed, EFace::Billboard, {0, 0, 10}, 0, 0.5f, 80, 260},

		{ETurtleVfx::Respawn, TEXT("T_VFX_Glow"), White * 0.5f, EFace::Billboard, {0, 0, 40}, 0, 1.0f, 140, 180, 3},
		{ETurtleVfx::Respawn, TEXT("T_VFX_Ring"), White, EFace::Ground, {0, 0, -90}, 0, 0.8f, 300, 60},
		{ETurtleVfx::Respawn, TEXT("T_VFX_Star"), White, EFace::Billboard, {0, 0, 80}, 0.2f, 0.8f, 60, 120, 1, 0, 60},

		{ETurtleVfx::Purchase, TEXT("T_VFX_Star"), Gold, EFace::Billboard, {0, 0, 60}, 0, 0.7f, 50, 110, 1, 200, 90},
		{ETurtleVfx::Purchase, TEXT("T_VFX_Star"), Gold, EFace::Billboard, {30, -30, 40}, 0.12f, 0.6f, 40, 90, 1, -200, 110},
		{ETurtleVfx::Purchase, TEXT("T_VFX_Glow"), Gold * 0.4f, EFace::Billboard, {0, 0, 40}, 0, 0.5f, 80, 160},
	};
}

ATurtleVfxActor::ATurtleVfxActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

void ATurtleVfxActor::Spawn(UWorld* World, ETurtleVfx Kind, const FVector& Location, float Yaw)
{
	if (!World || World->IsNetMode(NM_DedicatedServer)) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ATurtleVfxActor* Vfx = World->SpawnActor<ATurtleVfxActor>(Location, FRotator(0, Yaw, 0), Params)) Vfx->Build(Kind);
}

void ATurtleVfxActor::Build(ETurtleVfx InKind)
{
	Kind = InKind;
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/M_VFX_Additive.M_VFX_Additive"));
	if (!Plane || !Material) { Destroy(); return; }
	for (int32 i = 0; i < UE_ARRAY_COUNT(LayerDefs); ++i)
	{
		const FLayerDef& Def = LayerDefs[i];
		if (Def.Kind != Kind) continue;
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/VFX/Textures/%s.%s"), Def.Texture, Def.Texture));
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetStaticMesh(Plane);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetupAttachment(GetRootComponent());
		Mesh->SetHiddenInGame(true);
		Mesh->RegisterComponent();
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		if (Texture) Instance->SetTextureParameterValue(TEXT("Tex"), Texture);
		Instance->SetVectorParameterValue(TEXT("Color"), Def.Color);
		Mesh->SetMaterial(0, Instance);
		Layers.Add({Mesh, Instance, i});
		Duration = FMath::Max(Duration, Def.Delay + Def.Life);
	}
	Tick(0.f);
}

void ATurtleVfxActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age > Duration + 0.05f) { Destroy(); return; }
	const FRotator Facing = GetActorRotation();
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	for (const FLayer& Layer : Layers)
	{
		const FLayerDef& Def = LayerDefs[Layer.Def];
		const float Local = Age - Def.Delay;
		const float T = Local / Def.Life;
		Layer.Mesh->SetHiddenInGame(T < 0.f || T > 1.f);
		if (T < 0.f || T > 1.f) continue;
		// Quick fade-in, eased growth, smooth fade-out.
		const float Alpha = T < 0.15f ? T / 0.15f : FMath::Square(1.f - (T - 0.15f) / 0.85f);
		const float Size = FMath::Lerp(Def.StartSize, Def.EndSize, 1.f - FMath::Square(1.f - T));
		Layer.Material->SetScalarParameterValue(TEXT("Opacity"), Alpha);
		const FVector Location = GetActorLocation() + Facing.RotateVector(Def.Offset) + FVector(0, 0, Def.Rise * Local);
		FRotator Rotation(0, Facing.Yaw + Def.YawOffset + Def.Spin * Local, 0);
		FVector Scale(Size / 100.f, Size / 100.f, 1.f);
		if (Def.Face == EFace::Billboard)
		{
			const FVector ToCamera = Camera ? (Camera->GetCameraLocation() - Location).GetSafeNormal() : FVector::UpVector;
			// Plane normal (+Z) faces the camera; +X stays world-up so Aspect stretches vertically.
			const FQuat Base = FRotationMatrix::MakeFromZX(ToCamera, FVector::UpVector).ToQuat();
			Rotation = (FQuat(ToCamera, FMath::DegreesToRadians(Def.Spin * Local)) * Base).Rotator();
			Scale.X *= Def.Aspect;
		}
		Layer.Mesh->SetWorldLocationAndRotation(Location, Rotation);
		Layer.Mesh->SetWorldScale3D(Scale);
	}
}
