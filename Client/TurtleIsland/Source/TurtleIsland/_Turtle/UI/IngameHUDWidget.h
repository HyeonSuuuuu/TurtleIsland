// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IngameHUDWidget.generated.h"

class UEditableText;
class UScrollBox;
class UVerticalBox;
class UWidget;

/**
 *  인게임 메인 HUD(WBP_MainHUD)의 C++ 부모.
 *
 *  지금은 채팅 포커스만 담당한다. "엔터로 채팅을 친다"는 두 가지가 같이 움직여야 한다.
 *    1) 키보드 포커스 — 타이핑한 글자가 ChatInput으로 가게
 *    2) 입력 모드     — 게임과 UI 중 누가 입력을 먹을지
 *  포커스만 주면 WASD가 캐릭터를 계속 움직이고, 입력 모드만 바꾸면 글자가 아무 데도
 *  안 들어간다. FInputModeGameAndUI + SetWidgetToFocus 조합이면 포커스를 가진
 *  EditableText가 키 입력을 소비하므로 캐릭터가 같이 움직이지 않는다.
 *
 *  여는 쪽은 ATurtleIslandPlayerController(엔터), 닫는 쪽은 이 위젯(엔터/ESC)이다.
 *  ESC 메뉴와 겹치므로, 채팅 중 ESC는 채팅만 닫고 메뉴는 열지 않는다.
 */
UCLASS()
class TURTLEISLAND_API UIngameHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 채팅 입력창에 포커스를 준다. 이미 채팅 중이면 아무것도 안 한다. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void FocusChat();

	/** 채팅을 끝낸다. bSubmit이면 입력한 줄을 로그에 올린다. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void CloseChat(bool bSubmit);

	/** 채팅 입력 중인지. PC가 ESC를 어디로 보낼지 판단할 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Chat")
	bool IsChatFocused() const { return bChatFocused; }

	/** 로그에 한 줄 추가. 서버에서 받은 메시지도 이걸로 넣으면 된다. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void AppendChatLine(const FText& Line);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 포커스를 가진 동안에는 PC의 BindKey가 안 오므로 ESC를 여기서 받는다. */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP_MainHUD의 같은 이름 위젯에 자동으로 붙는다. 없어도 크래시 안 나게 Optional. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableText> ChatInput;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> MessageContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> ChatLogScrollBox;

	/** 로그에 남길 최대 줄 수. 넘으면 오래된 것부터 지운다. */
	UPROPERTY(EditDefaultsOnly, Category = "Chat", meta = (ClampMin = "10"))
	int32 MaxChatLines = 60;

	/** 엔터로 확정 / ESC로 취소될 때 불린다. */
	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

private:
	/** 새 줄의 폰트·색을 기존 자리표시자 줄에서 베껴 온다. */
	void CacheLineStyleFromPlaceholder();

	bool bChatFocused = false;

	FSlateFontInfo CachedLineFont;
	FSlateColor CachedLineColour;
	bool bLineStyleCached = false;
};
