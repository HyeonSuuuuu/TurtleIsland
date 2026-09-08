// Fill out your copyright notice in the Description page of Project Settings.

#include "Network/NetSyncComponent.h"
#include "Network/NetSubsystem.h"
#include "Gameplay/AgentMovementInput.h"
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
		Input = Owner->FindComponentByClass<UAgentMovementInput>();
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

uint16 UNetSyncComponent::BuildInputFlag() const
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

	// 기준축은 방향을 만든 입력 컴포넌트에게 묻는다. 여기서 다시 유도하면
	// 입력 쪽 기준이 바뀌었을 때 조용히 어긋난다.
	const FRotator YawRotation(0.f, Input->GetViewYaw(), 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	const FVector WorldDir(World.X, World.Y, 0.f);
	const float ForwardAmount = FVector::DotProduct(WorldDir, Forward);
	const float RightAmount   = FVector::DotProduct(WorldDir, Right);

	uint16 Flag = 0;
	if (ForwardAmount >  0.1f) { Flag |= INPUT_FLAG_W; }
	if (RightAmount   < -0.1f) { Flag |= INPUT_FLAG_A; }
	if (ForwardAmount < -0.1f) { Flag |= INPUT_FLAG_S; }
	if (RightAmount   >  0.1f) { Flag |= INPUT_FLAG_D; }
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

	const uint16 Flag = PendingFlag;
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
