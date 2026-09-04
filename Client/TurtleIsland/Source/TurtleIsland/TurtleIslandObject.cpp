// Fill out your copyright notice in the Description page of Project Settings.


#include "TurtleIslandObject.h"

// Sets default values
ATurtleIslandObject::ATurtleIslandObject()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ATurtleIslandObject::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ATurtleIslandObject::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ATurtleIslandObject::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

