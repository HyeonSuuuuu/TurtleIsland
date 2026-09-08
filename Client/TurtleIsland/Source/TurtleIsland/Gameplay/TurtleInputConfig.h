// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TurtleInputConfig.generated.h"

class UInputAction;

/** 스킬 키 하나. 어떤 입력이 몇 번 스킬인지의 매핑을 데이터로 둔다 */
USTRUCT(BlueprintType)
struct FSkillInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skill")
	UInputAction* Action = nullptr;

	/** 0 = 기본공격 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skill")
	int32 SkillId = 0;
};

/**
 * 키와 게임 의미의 매핑을 담는 데이터 에셋. Lyra 의 ULyraInputConfig 와 같은 자리다.
 *
 * 입력을 두 갈래로 나눈다 —
 *   Native : 이동/시선/점프처럼 매 프레임 값이 필요한 연속 입력. C++ 이 직접 처리한다.
 *   Skill  : 스킬 키. 개별 함수가 아니라 ID 를 실어 하나의 델리게이트로 보낸다.
 *
 * 스킬을 추가할 때 C++ 도 컴포넌트도 고치지 않는다. SkillActions 에 행 하나를 더한다.
 * (Epic 도 이동은 특별 취급하고 어빌리티만 일반화했다. 성질이 다르기 때문이다.)
 */
UCLASS(BlueprintType)
class TURTLEISLAND_API UTurtleInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// ---- Native: 연속 입력 ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Native")
	UInputAction* MoveAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Native")
	UInputAction* LookAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Native")
	UInputAction* MouseLookAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Native")
	UInputAction* JumpAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Native")
	UInputAction* InteractAction = nullptr;

	// ---- Skill: 데이터로 확장되는 입력 ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Skill")
	TArray<FSkillInputAction> SkillActions;
};
