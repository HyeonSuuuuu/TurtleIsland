#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NpcNameplateComponent.generated.h"

/**
 * NPC 머리 위 이름표와 "F 대화하기" 안내. 그리는 건 MainHUDWidget 이 몬스터 이름표와 함께 한다.
 * 안내는 플레이어가 NPC 의 상호작용 구체(BP 가 CurrentInteractable 을 정하는 그 구체) 안에 있을 때만 뜬다.
 */
UCLASS(ClassGroup=(Turtle), meta=(BlueprintSpawnableComponent))
class TURTLEISLAND_API UNpcNameplateComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Nameplate")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Nameplate")
	FText PromptText = FText::FromString(TEXT("대화하기"));

	/** 메시 꼭대기에서 이름표까지 띄울 높이(cm) */
	UPROPERTY(EditAnywhere, Category="Nameplate", meta=(Units="cm"))
	float HeightOffset = 25.f;

	bool IsPlayerInRange(const AActor* Player) const;
	FVector GetLabelLocation() const;

	/** 등록된 모든 이름표. HUD 가 월드의 액터를 전부 훑지 않도록 컴포넌트가 스스로 등록한다. */
	static const TArray<TWeakObjectPtr<UNpcNameplateComponent>>& GetAll();

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};
