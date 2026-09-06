// Fill out your copyright notice in the Description page of Project Settings.

#include "Network/NetSyncComponent.h"
#include "Network/NetSubsystem.h"
#include "Gameplay/AgentInputComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Protocol.h"

UNetSyncComponent::UNetSyncComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UNetSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner)
	{
		Input = Owner->FindComponentByClass<UAgentInputComponent>();
	}

	// 인게임 진입 통지. 이 컴포넌트는 멀티에서만 존재하므로 모드 검사가 필요 없다.
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UNetSubsystem* Net = GI ? GI->GetSubsystem<UNetSubsystem>() : nullptr;
	if (Net && Owner)
	{
		const FVector Loc = Owner->GetActorLocation();
		Net->NotifyIngameReady(Loc.X, Loc.Y, Owner->GetActorRotation().Yaw);
	}
}

uint8 UNetSyncComponent::BuildInputFlag() const
{
	if (!Input)
	{
		return 0;
	}

	const FVector2D World = Input->GetMovementInput();
	if (World.IsNearlyZero())
	{
		return 0;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	float ViewYaw = OwnerPawn ? OwnerPawn->GetActorRotation().Yaw : 0.f;
	if (OwnerPawn)
	{
		if (const AController* C = OwnerPawn->GetController())
		{
			ViewYaw = C->GetControlRotation().Yaw;
		}
	}

	// 월드 방향을 뷰 기준으로 되돌린다 (ULocalInputProvider 변환의 역)
	const FRotator YawRotation(0.f, ViewYaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	const FVector WorldDir(World.X, World.Y, 0.f);
	const float ForwardAmount = FVector::DotProduct(WorldDir, Forward);
	const float RightAmount   = FVector::DotProduct(WorldDir, Right);

	uint8 Flag = 0;
	if (ForwardAmount >  0.1f) { Flag |= 1; } // W
	if (RightAmount   < -0.1f) { Flag |= 2; } // A
	if (ForwardAmount < -0.1f) { Flag |= 4; } // S
	if (RightAmount   >  0.1f) { Flag |= 8; } // D
	return Flag;
}

void UNetSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 전송 주기보다 짧게 눌렸다 떼는 입력을 놓치지 않도록 매 프레임 OR 로 누적한다
	// (원래 캐릭터 Tick 이 하던 방식과 같다)
	PendingFlag |= BuildInputFlag();

	const float Interval = SendRate > 0.f ? (1.f / SendRate) : (1.f / 30.f);

	Accumulated += DeltaTime;
	if (Accumulated < Interval)
	{
		return;
	}
	Accumulated -= Interval;

	const uint8 Flag = PendingFlag;
	PendingFlag = 0;
	if (Flag == 0)
	{
		return;
	}

	AActor* Owner = GetOwner();
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UNetSubsystem* Net = GI ? GI->GetSubsystem<UNetSubsystem>() : nullptr;
	if (!Owner || !Net)
	{
		return;
	}

	CS_Move MovePacket{};
	MovePacket.size      = CS_MOVE_PACKET_SIZE;
	MovePacket.id        = static_cast<uint16>(PACKET_ID::CS_MOVE);
	MovePacket.inputFlag = Flag;
	MovePacket.yaw       = Owner->GetActorRotation().Yaw;

	const FVector Loc = Owner->GetActorLocation();
	MovePacket.x = Loc.X;
	MovePacket.y = Loc.Y;

	Net->SendPacket(MovePacket);
}
