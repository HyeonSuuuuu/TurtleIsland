// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "ShopWidget.generated.h"

class UEditableText;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

/** 왼쪽 목록의 포션 칸 하나를 감싸는 투명 버튼. 누르면 1개 구매. */
UCLASS()
class TURTLEISLAND_API UShopItemButton : public UButton
{
	GENERATED_BODY()
public:
	UShopItemButton();
	void Setup(int32 InItemId);
	int32 GetItemId() const { return ItemId; }
private:
	UFUNCTION() void Buy();
	int32 ItemId = 0;
};

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

	/** 대화 로그에 말풍선을 하나 붙인다. bFromNpc 면 왼쪽, 아니면 오른쪽 정렬. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void AppendMessage(const FString& Message, bool bFromNpc);

	UFUNCTION(BlueprintPure, Category = "Shop")
	int32 GetNpcIndex() const { return NpcIndex; }

protected:
	virtual void NativeOnInitialized() override;
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

	/** NPC 말풍선 바탕(UI 강조색 계열). */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Bubble")
	FLinearColor NpcBubbleColour = FLinearColor(0.054480f, 0.250158f, 0.230740f, 1.f);

	/** 내 말풍선 바탕(시안). */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Bubble")
	FLinearColor SelfBubbleColour = FLinearColor(0.160f, 0.780f, 0.860f, 1.f);

	/** NPC 말풍선 글자색(본문 회색). */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Bubble")
	FSlateColor NpcTextColour = FSlateColor(FLinearColor(0.701102f, 0.701102f, 0.701102f, 1.f));

	/** 내 말풍선 글자색. 바탕이 밝아서 검정이어야 읽힌다. */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Bubble")
	FSlateColor SelfTextColour = FSlateColor(FLinearColor(0.02f, 0.05f, 0.06f, 1.f));

	/** 화자 이름(카톡처럼 상대 것만, 연속이면 생략) 색. */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Bubble")
	FSlateColor SpeakerNameColour = FSlateColor(FLinearColor(0.701102f, 0.701102f, 0.701102f, 0.65f));

	/** 말풍선 최대 폭(px). 넘으면 줄바꿈된다. 이게 없으면 패널 끝까지 늘어나 카톡 느낌이 안 난다. */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Bubble")
	float BubbleMaxWidth = 420.f;

	/** 말풍선 모서리 반경. 꼬리 쪽 모서리 하나만 작게 줘서 방향을 준다. */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Bubble")
	float BubbleCornerRadius = 14.f;

	/**
	 *  켜면 스크롤을 위로 올려 지난 대화를 보는 중일 때 따라 내려가지 않는다.
	 *  기본은 끔 — 상점 대화는 짧고 답이 3~4초 뒤에 오므로 항상 최신이 보이는 게 낫다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Shop")
	bool bFollowOnlyWhenAtBottom = false;

	/**
	 *  "맨 아래를 보고 있다"로 칠 여유(px). 위젯을 막 붙이거나 뗀 직후에는 레이아웃이
	 *  아직 갱신되지 않아 스크롤 끝값이 몇 px 어긋난다. 1px 로 재면 그 순간을
	 *  "위로 올려 본 것"으로 오해해 그 뒤로 따라 내려가지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Shop")
	float FollowSlackPx = 32.f;

	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleSendClicked();

	/** 델리게이트가 값 전달이라 참조로 받으면 안 붙는다. */
	UFUNCTION()
	void HandleNpcChat(FString Say);

	/** 흥정 가격이 붙은 상품의 가격 글자색. */
	UPROPERTY(EditDefaultsOnly, Category = "Shop|Products")
	FSlateColor QuotePriceColour = FSlateColor(FLinearColor(0.35f, 0.95f, 0.45f, 1.f));

	/** 가격·골드가 바뀌면 상품 칸의 가격 표시와 구매 가능 여부를 갱신한다. */
	UFUNCTION()
	void RefreshProducts();

	UFUNCTION()
	void HandleShopFeedback(FString Message);

private:
	class UNetSubsystem* GetNet() const;

	/** 디자이너 줄에서 폰트·색을 베껴 온다. 새로 만드는 줄에 그대로 쓴다. */
	void CacheLineStyle();

	/** 내 말풍선 위에 붙일 이름. 로그인 ID 를 쓰고, 없으면 "나". */
	FString GetSelfDisplayName() const;

	/** 말풍선 하나(이름표 포함)를 만들어 컨테이너에 붙인다. */
	void AddBubble(const FString& Text, bool bFromNpc, bool bIsTyping);

	/** 답을 기다리는 동안 띄우는 "···" 말풍선. 답이 오면 지운다. */
	void ShowTyping();
	void HideTyping();

	/** 맨 아래를 보고 있었는지. bFollowOnlyWhenAtBottom 이 꺼져 있으면 항상 참. */
	bool WasScrolledToBottom() const;

	/** 레이아웃이 갱신된 다음 틱에 실제로 끝까지 내린다. */
	void ScrollToBottomNow();

	/** 입력창에 키보드 포커스. */
	void FocusInput();

	/** 마지막으로 표시한 상인 호감도. 아직 모르면 -1. 바뀌면 대화창에 알린다. */
	int32 ShownAffinity = -1;

	/** 대화 상대 NPC. OpenShop 으로 정해진다. */
	int32 NpcIndex = 0;

	FSlateFontInfo CachedFont;
	bool bStyleCached = false;

	/** 직전 화자. 같은 화자가 이어지면 이름표를 생략한다(카톡과 같은 규칙). */
	bool bLastWasNpc = false;
	bool bHasLastSpeaker = false;

	/** 지금 떠 있는 "···" 말풍선. 없으면 nullptr. */
	UPROPERTY()
	TObjectPtr<UWidget> TypingBubble;

	/** 가운데 정렬 안내 줄(구매 결과 등). */
	void AddSystemLine(const FString& Text);

	/** 디자이너의 Frame* 칸 중 포션 아이콘이 든 것. 가격 TextBlock 은 없을 수 있다. */
	UPROPERTY()
	TArray<TObjectPtr<UShopItemButton>> ProductButtons;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> ProductPrices;
	TArray<FSlateColor> ProductPriceColours;
};
