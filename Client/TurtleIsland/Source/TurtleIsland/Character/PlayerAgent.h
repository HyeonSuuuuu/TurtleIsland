// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Character/TurtleAgent.h"
#include "PlayerAgent.generated.h"

class UPlayerStatsComponent;
class ULocalInputProvider;
class ULocalMoverComponent;
class USkillInputComponent;
class UInteractionComponent;


/**
 *  로컬 플레이어가 조종하는 Agent 의 조립 클래스.
 *
 *  로직은 전부 컴포넌트로 나갔다. 여기 남은 일은 두 가지뿐 —
 *   1) 로컬 플레이어용 컴포넌트 세트를 붙인다
 *   2) 싱글/멀티 분기를 BeginPlay 에서 딱 한 번 본다 (NetSync 를 붙일지 말지)
 *
 *  BP_MyCharacter 가 이 클래스를 상속하고 있어 남겨둔다. 블루프린트를 ATurtleAgent 로
 *  리페어런팅한 뒤에는 이 클래스를 지워도 된다.
 */
UCLASS(abstract)
class APlayerAgent : public ATurtleAgent
{
	GENERATED_BODY()

	/** 레벨/경험치/골드 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats", meta = (AllowPrivateAccess = "true"))
	UPlayerStatsComponent* Stats;

	/** 키보드/마우스 입력 소스 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	ULocalInputProvider* LocalInput;

	/** CharacterMovementComponent 위임 Mover */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	ULocalMoverComponent* Mover;

	/** 좌클릭/Q/E → CS_ATTACK. 판정도 모션도 여기서 하지 않는다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkillInputComponent* SkillInput;

	/** F → 근처 대상 탐색 + 상호작용 델리게이트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UInteractionComponent* Interaction;

protected:

public:

	/** Constructor */
	APlayerAgent();

protected:

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;

public:

	/** 스탯 컴포넌트 (몬스터 보상 지급/HUD 바인딩용) */
	UFUNCTION(BlueprintPure, Category="Stats")
	UPlayerStatsComponent* GetStats() const { return Stats; }

};
