// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class UInputAction;
class UInputComponent;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractTargetChanged, AActor*, NewTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteracted, AActor*, Target);

/**
 * 상호작용(F) 담당. 로컬 플레이어에게만 붙는다.
 *
 * 전투와 분리한 이유 — 상호작용은 서버 판정이 아니라 UI 를 여는 일이다.
 * 상점 NPC 대화창을 띄우는 것까지가 여기 몫이고, 실제 거래는 그 UI 가 패킷을 보낸다.
 *
 * 대상 판별은 태그가 아니라 "상호작용 태그를 가진 액터" 로 하되, 무엇을 할지는
 * 블루프린트가 OnInteracted 에 붙어서 정한다. 여기서 상점을 직접 알 필요는 없다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* InteractAction = nullptr;

	/** 이 거리 안에 있어야 상호작용 대상이 된다 */
	UPROPERTY(EditAnywhere, Category="Interaction")
	float Range = 300.f;

	/** 대상 탐색 주기(초). 매 프레임 훑을 필요는 없다 */
	UPROPERTY(EditAnywhere, Category="Interaction")
	float ScanInterval = 0.2f;

	/** 이 태그를 가진 액터만 대상으로 본다 */
	UPROPERTY(EditAnywhere, Category="Interaction")
	FName InteractableTag = TEXT("Interactable");

	/** 사거리 안 대상이 바뀔 때. "F 를 눌러 대화" 같은 안내를 여기 붙인다 */
	UPROPERTY(BlueprintAssignable, Category="Interaction")
	FOnInteractTargetChanged OnInteractTargetChanged;

	/** 실제로 F 를 눌렀을 때. 상점 UI 열기 등을 여기 붙인다 */
	UPROPERTY(BlueprintAssignable, Category="Interaction")
	FOnInteracted OnInteracted;

	UFUNCTION(BlueprintPure, Category="Interaction")
	AActor* GetCurrentTarget() const { return CurrentTarget; }

	/** 폰의 SetupPlayerInputComponent 에서 불러준다 */
	void BindInput(UInputComponent* PlayerInputComponent);

protected:
	void HandleInteract(const FInputActionValue& Value);

	/** 사거리 안에서 가장 가까운 대상을 찾는다 */
	AActor* FindNearestTarget() const;

private:
	UPROPERTY(Transient)
	AActor* CurrentTarget = nullptr;

	float ScanAccum = 0.f;
};
