// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/TurtleObject.h"

// Sets default values
ATurtleObject::ATurtleObject()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ATurtleObject::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ATurtleObject::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ATurtleObject::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

