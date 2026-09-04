// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "ShopWidget.generated.h"

class UButton;
class UEditableText;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

/**
 *  상점 NPC 대화창(WBP_Shop 오른쪽 패널).
 *
 *  와이어는 UNetSubsystem 이 다 한다:
 *    클라 CS_NPC_CHAT → 게임서버 → SA_AI → AI 서버 → AS_AI → 게임서버 → SC_NPC_CHAT → 클라
 *  게임서버는 보낸 사람에게 에코를 돌려주지 않으므로 내가 친 줄은 여기서 직접 찍는다.
 *
 *  표시는 ChatLogWidget 과 같은 방식이다 — 줄마다 TextBlock 을 만들어
 *  MessageContainer 에 쌓는다. 고정 슬롯에 덮어쓰면 스크롤이 안 생겨 지난 대화를 못 본다.
 *  디자이너에 남겨둔 줄은 폰트·색 견본으로만 쓰고 시작할 때 비운다.
 */
UCLASS()
class TURTLEISLAND_API UShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 *  대화 상대를 정하고 입력창에 포커스를 준다.
	 *  위젯 생성·AddToViewport·입력모드·커서·isShopOpen 은 BP_IngamePlayerController 의
	 *  OpenShop/CloseShop 이벤트가 이미 관리한다. 여기서 또 만지면 두 상태가 어긋나므로
	 *  이 위젯은 대화만 맡는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void OpenShop(int32 InNpcIndex);

	/** 입력창 내용을 NPC 에게 보내고 비운다. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void SubmitMessage();

	/** 입력창을 안 거치고 바로 보낸다(퀵 대사 버튼 등). */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void SendToNpc(const FString& Message);

	/** 대화 로그에 줄을 하나 붙인다. bFromNpc 면 NPC 색으로 찍는다. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void AppendLine(const FString& Line, bool bFromNpc = false);

	UFUNCTION(BlueprintPure, Category = "Shop")
	int32 GetNpcIndex() const { return NpcIndex; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// WBP_Shop 의 같은 이름 위젯에 자동으로 붙는다. 없어도 크래시 나지 않는다.
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UEditableText> Input_Message;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> DialogueScrollBox;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> MessageContainer;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Send;

	/** 로그에 찍을 NPC 이름. */
	UPROPERTY(EditDefaultsOnly, Category = "Shop")
	FString NpcDisplayName = TEXT("상인");

	/**
	 *  새로 만드는 줄의 폰트. 반드시 한글 글리프가 있는 폰트여야 한다 —
	 *  비워두면 엔진 기본 폰트로 나오고 한글이 깨진다.
	 *  MessageContainer 안에 TextBlock 이 하나라도 있으면 그쪽 스타일을 우선 쓴다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Shop")
	FSlateFontInfo LineFont;

	/** 내가 친 줄의 색(본문 회색). */
	UPROPERTY(EditDefaultsOnly, Category = "Shop")
	FSlateColor LineColour = FSlateColor(FLinearColor(0.701102f, 0.701102f, 0.701102f, 1.f));

	/** NPC 줄의 색(UI 강조색). */
	UPROPERTY(EditDefaultsOnly, Category = "Shop")
	FSlateColor NpcColour = FSlateColor(FLinearColor(0.160f, 0.780f, 0.860f, 1.f));

	/** 새 줄이 와도, 스크롤을 위로 올려 지난 대화를 보는 중이면 따라 내려가지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Shop")
	bool bFollowOnlyWhenAtBottom = true;

	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleSendClicked();

	/** 델리게이트가 값 전달이라 참조로 받으면 안 붙는다. */
	UFUNCTION()
	void HandleNpcChat(FString Say);

private:
	class UNetSubsystem* GetNet() const;

	/** 디자이너 줄에서 폰트·색을 베껴 온다. 새로 만드는 줄에 그대로 쓴다. */
	void CacheLineStyle();

	/** 대화 상대 NPC. OpenShop 으로 정해진다. */
	int32 NpcIndex = 0;

	FSlateFontInfo CachedFont;
	bool bStyleCached = false;
};
