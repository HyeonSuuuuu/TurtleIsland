#include "Gameplay/NpcNameplateComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	TArray<TWeakObjectPtr<UNpcNameplateComponent>> Registry;
}

const TArray<TWeakObjectPtr<UNpcNameplateComponent>>& UNpcNameplateComponent::GetAll()
{
	return Registry;
}

void UNpcNameplateComponent::OnRegister()
{
	Super::OnRegister();
	Registry.AddUnique(this);
}

void UNpcNameplateComponent::OnUnregister()
{
	Registry.Remove(this);
	Super::OnUnregister();
}

bool UNpcNameplateComponent::IsPlayerInRange(const AActor* Player) const
{
	const USphereComponent* Sphere = GetOwner() ? GetOwner()->FindComponentByClass<USphereComponent>() : nullptr;
	return Player && Sphere && Sphere->IsOverlappingActor(Player);
}

FVector UNpcNameplateComponent::GetLabelLocation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner) return FVector::ZeroVector;
	if (const USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>())
		return Mesh->Bounds.Origin + FVector(0, 0, Mesh->Bounds.BoxExtent.Z + HeightOffset);
	return Owner->GetActorLocation() + FVector(0, 0, 200 + HeightOffset);
}
