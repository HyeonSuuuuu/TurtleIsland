// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Character/TurtleAgent.h"
#include "PlayerAgent.generated.h"

class UPlayerStatsComponent;
class ULocalMoverComponent;
class USpringArmComponent;
class UCameraComponent;
class UPlayerInputComponent;
class ULocalMovementInput;
class UInteractionComponent;


/**
 *  로컬 플레이어가 조종하는 Agent 의 조립 클래스.
 *
 *  로직은 전부 컴포넌트로 나갔다. 여기 남은 일은 두 가지뿐 —
 *   1) 로컬 플레이어용 컴포넌트 세트를 붙인다
 *   2) 싱글/멀티 분기를 BeginPlay 에서 딱 한 번 본다 (NetSync 를 붙일지 말지)
 *
 *  입력 부품들은 멤버로 들지 않는다. Agent 가 그것들을 알 이유가 없고,
 *  서로 델리게이트로 붙기 때문이다. 여기서 입력에 하는 일은 바인딩 지점을
 *  한 번 찾아 넘겨주는 것뿐이다.
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

	/** 카메라 붐. 컨트롤 회전을 따라간다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	/** CharacterMovementComponent 위임 Mover */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	ULocalMoverComponent* Mover;

	// 아래 참조들은 Agent 가 쓰려고 드는 게 아니다. 언리얼에서 네이티브 컴포넌트를
	// 블루프린트에서 설정하려면 소유 클래스가 리플렉션으로 도달 가능한 참조를 들고
	// 있어야 한다. Agent 코드는 이들을 한 번도 호출하지 않는다 —
	// 부품끼리 델리게이트로 붙기 때문이다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UPlayerInputComponent* PlayerInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	ULocalMovementInput* LocalInput;

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
