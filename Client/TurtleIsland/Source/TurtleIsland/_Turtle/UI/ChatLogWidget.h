// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "ChatLogWidget.generated.h"

class UButton;
class UEditableText;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

/**
 *  인게임 HUD 의 채팅.
 *
 *  네트워크는 UNetSubsystem 이 이미 다 한다 (SendChat / OnChatMessage / ChatLines).
 *  언리얼 기본 리플리케이션이 아니라 자체 소켓 서버라 GameMode 나 Server RPC 는 안 쓴다.
 *
 *  표시는 줄마다 TextBlock 을 하나씩 만들어 MessageContainer 에 쌓는다. 고정 슬롯에
 *  덮어쓰는 방식은 ScrollBox 안 내용이 늘 화면 높이라 스크롤이 안 생겨서 지난 대화를
 *  못 본다. 디자이너에 박아둔 줄은 스타일 견본으로만 쓰고 시작할 때 비운다.
 *
 *  엔터로 채팅을 여는 것(포커스 주기)은 블루프린트가 담당한다.
 */
UCLASS()
class TURTLEISLAND_API UChatLogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 입력창 내용을 서버로 보내고 비운다. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void SubmitChat();

	/** 문자열을 바로 보낸다. 입력창을 안 거치는 경로(시스템 메시지 등)용. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void SendChatText(const FString& Message);

	/** UNetSubsystem::ChatLines 를 화면에 반영한다. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void RefreshChatLines();

	/** 입력이 끝났을 때 포커스를 게임 뷰포트로 되돌린다. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void ReturnFocusToGame();

	/** 입력창에 포커스를 준다. 연속 입력을 위해 전송 직후에도 부른다. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void FocusChatInput();

	/** 지금 채팅 입력 중인지. 블루프린트에서 엔터 키를 가로챌지 판단할 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Chat")
	bool IsChatInputActive() const { return bChatInputActive; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// WBP_MainHUD 의 같은 이름 위젯에 자동으로 붙는다. 없어도 크래시 나지 않는다.
	// BlueprintReadOnly 가 있어야 리페어런트 후에도 블루프린트 그래프에서 읽을 수 있다.
	UPROPERTY(BlueprintReadOnly, Category = "Chat", meta = (BindWidgetOptional))
	TObjectPtr<UEditableText> ChatInput;

	UPROPERTY(BlueprintReadOnly, Category = "Chat", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> MessageContainer;

	UPROPERTY(BlueprintReadOnly, Category = "Chat", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> ChatLogScrollBox;

	/** 전송 버튼. 마우스만으로도 보낼 수 있게 한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Chat", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ChatSendBtn;

	/** 엔터로 보낸 뒤에도 입력창에 남아 연속으로 칠지. ESC 로는 언제든 빠져나온다. */
	UPROPERTY(EditDefaultsOnly, Category = "Chat")
	bool bKeepFocusAfterSend = true;

	/**
	 *  새로 만드는 줄의 폰트. 반드시 한글 글리프가 있는 폰트여야 한다 —
	 *  비워두면 엔진 기본 폰트로 나오고 한글이 깨진다.
	 *  MessageContainer 안에 TextBlock 이 하나라도 있으면 그쪽 스타일을 우선 쓴다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Chat")
	FSlateFontInfo LineFont;

	/** 새로 만드는 줄의 글자색. */
	UPROPERTY(EditDefaultsOnly, Category = "Chat")
	FSlateColor LineColour = FSlateColor(FLinearColor(0.70f, 0.70f, 0.70f, 1.f));

	/** 새 메시지가 와도, 스크롤을 위로 올려 지난 대화를 보는 중이면 따라 내려가지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Chat")
	bool bFollowOnlyWhenAtBottom = true;

	/**
	 *  채팅 중에 마우스 커서를 띄워 UI 를 만질 수 있게 한다(로그 스크롤 등).
	 *  입력 모드를 Game and UI 로 바꾸므로 그동안 마우스 시점 조작은 멈춘다.
	 *  만약 이걸 켠 뒤 타이핑이 안 되면 꺼라 — 뷰포트가 키를 먼저 먹는 경우가 있다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Chat")
	bool bMouseCursorWhileChatting = true;

	/**
	 *  입력창이 포커스를 잃으면(뷰포트를 클릭했다든지) 채팅을 자동으로 끝낸다.
	 *  이게 없으면 Game and UI 상태로 남아 커서가 계속 떠 있고 시점도 안 돌아온다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Chat")
	bool bExitChatOnFocusLoss = true;

	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleSendClicked();

	/** 줄이 도착할 때 불린다. 인자는 안 쓰고 배열 상태로 화면을 맞춘다. */
	UFUNCTION()
	void HandleChatMessage(FString Line);   // 델리게이트가 값 전달이라 참조로 받으면 안 붙는다

private:
	class UNetSubsystem* GetNet() const;

	/** 디자이너 줄에서 폰트·색을 베껴 온다. 새로 만드는 줄에 그대로 쓴다. */
	void CacheLineStyle();

	/** 입력창이 아직 포커스를 쥐고 있는지 지켜본다. 채팅 중에만 돈다. */
	void WatchChatFocus();

	/** 채팅 중인가. 시점/이동 무시 카운터를 정확히 한 번씩만 올리고 내리려고 들고 있다. */
	bool bChatInputActive = false;

	/** 포커스를 놓친 채 흐른 시간. 한 틱 튀는 것과 진짜 이탈을 구분한다. */
	float FocusLostSeconds = 0.f;

	FTimerHandle FocusWatchdogHandle;

	FSlateFontInfo CachedFont;
	FSlateColor CachedColour;
	bool bStyleCached = false;

	/** 지금까지 만들어 붙인 줄 수. 늘어난 만큼만 추가하려고 들고 있다. */
	int32 BuiltLineCount = 0;
};
