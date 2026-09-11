// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NetSyncComponent.generated.h"

class UAgentMovementInput;

/**
 * 로컬 폰의 이동을 서버로 올린다. 멀티에서만 존재한다.
 *
 * 이 컴포넌트가 있다는 사실 자체가 "멀티 모드"를 뜻하므로, 여기 있는 코드는
 * IsSinglePlay() 를 묻지 않는다. 싱글이면 애초에 붙지 않는다.
 * 덕분에 캐릭터 클래스에서 Protocol.h 의존이 사라진다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UNetSyncComponent : public UActorComponent
{
	GENERATED_BODY()

private:
	UPROPERTY(Transient)
	UAgentMovementInput* Input = nullptr;

	float Accumulated = 0.f;
	FVector LastPosition = FVector::ZeroVector;
	float LastYaw = 0.f;
	bool bLastAirborne = false;
	bool bSentPose = false;
	uint16 LastFlag = 0;

	/** 전송 주기 사이에 누적된 입력 비트 */
	uint16 PendingFlag = 0;

protected:
	virtual void BeginPlay() override;

	/**
	 * 월드 XY 방향을 컨트롤 yaw 로 역회전시켜 WASD 4비트로 되돌린다.
	 * 서버는 4비트만 받으므로 손실이 없다.
	 */
	uint16 BuildInputFlag() const;

public:
	UNetSyncComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 초당 전송 횟수 */
	UPROPERTY(EditAnywhere, Category="Net")
	float SendRate = 30.f;
};
