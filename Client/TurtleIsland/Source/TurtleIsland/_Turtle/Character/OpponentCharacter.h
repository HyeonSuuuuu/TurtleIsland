// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "OpponentCharacter.generated.h"

UCLASS()
class TURTLEISLAND_API AOpponentCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AOpponentCharacter();

	// 서버에서 받은 위치/회전 목표 갱신
	void UpdateNetState(const FVector& InLocation, const FRotator& InRotation);

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// 애님BP가 읽는 현재 속도(2D)
	UPROPERTY(BlueprintReadOnly, Category = "Net")
	float Speed = 0.f;

private:
	FVector  TargetLocation = FVector::ZeroVector;
	FRotator TargetRotation = FRotator::ZeroRotator;
	bool     bHasTarget = false;

	// 위치/회전 보간 속도
	UPROPERTY(EditAnywhere, Category = "Net")
	float InterpSpeed = 12.f;
};
