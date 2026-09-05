// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TurtleObject.generated.h"

UCLASS()
class TURTLEISLAND_API ATurtleObject : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ATurtleObject();
	
	/** 최대 체력 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage")
	float MaxHP = 100.f;

	/** 현재 체력. BeginPlay 에서 MaxHP 로 채운다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Damage")
	float CurrentHP = 0.f;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

};
