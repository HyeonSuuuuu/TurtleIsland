// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerInputComponent.generated.h"

class UInputComponent;
class UTurtleInputConfig;
struct FInputActionValue;

/**
 * 로컬 플레이어의 키 입력을 모으는 단 하나의 지점. Lyra 의 ULyraHeroComponent 자리다.
 *
 * 프로젝트에서 Enhanced Input 을 바인딩하는 곳은 여기뿐이고, 소비자들은 델리게이트를
 * 구독할 뿐 자기 InputAction 을 갖지 않는다.
 *
 * 게임 의미(어떤 키가 몇 번 스킬인지)는 여기가 아니라 UTurtleInputConfig 에 있다.
 * 그래서 스킬이 늘어도 이 클래스는 바뀌지 않는다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UPlayerInputComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	void HandleMove(const FInputActionValue& Value);
	void HandleMoveEnd(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpStart(const FInputActionValue& Value);
	void HandleJumpEnd(const FInputActionValue& Value);
	void HandleInteract(const FInputActionValue& Value);

	/** 스킬 ID 를 페이로드로 받는다. 스킬이 몇 개든 이 함수 하나면 된다 */
	void HandleSkill(const FInputActionValue& Value, int32 SkillId);

public:
	UPlayerInputComponent();

	/** 키 ↔ 의미 매핑. 캐릭터마다 다른 스킬 세트를 주고 싶으면 여기만 바꾸면 된다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	UTurtleInputConfig* InputConfig = nullptr;

	/** 폰의 SetupPlayerInputComponent 에서 한 번만 불러준다 */
	void BindInput(UInputComponent* PlayerInputComponent);

	// ---- 구독 지점 ----
	// 이동 축은 컨트롤 회전 기준 (Right, Forward). 월드로 돌리는 건 소비자 몫이다.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAxis2D, const FVector2D&);
	DECLARE_MULTICAST_DELEGATE(FOnButton);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSkill, int32);

	FOnAxis2D OnMoveAxis;
	FOnButton OnMoveReleased;
	FOnAxis2D OnLookAxis;
	FOnButton OnJumpPressed;
	FOnButton OnJumpReleased;
	FOnSkill  OnSkillPressed;
	FOnButton OnInteractPressed;
};
