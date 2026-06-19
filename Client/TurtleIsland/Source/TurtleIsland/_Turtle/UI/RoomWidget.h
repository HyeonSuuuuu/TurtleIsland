// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomWidget.generated.h"

class UTextBlock;
class UButton;
class UBorder;
class UNetSubsystem;

/**
 * 방 대기실 UI (Lv2_Room HUD 오버레이).
 * 레이아웃은 WBP_RoomUI 디자이너에서 구성하고, 여기서는 meta=(BindWidget)으로
 * 이름이 일치하는 위젯에 연결해 텍스트/버튼/슬롯 상태만 갱신한다.
 * (로비/로그인 위젯과 동일한 패턴)
 *
 * 디자이너에서 만들어야 하는 위젯 이름:
 *   Text_Title, Text_Host, Text_Count        : TextBlock
 *   Btn_Leave, Btn_Start                      : Button
 *   Slot0_Name~Slot3_Name, Slot0_Tag~Slot3_Tag: TextBlock (참가자 4슬롯)
 *   Slot0_Row~Slot3_Row                       : Border (선택, 로컬 플레이어 강조용)
 */
UCLASS()
class TURTLEISLAND_API URoomWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 현재 참가자 상태로 슬롯/카운트/버튼 갱신
	void RefreshMembers();

	UFUNCTION()
	void OnLeaveClicked();

	UFUNCTION()
	void OnStartClicked();

	UFUNCTION()
	void HandleMembersUpdated();

	// ── 방 정보 ──
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_Title;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_Host;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_Count;

	// ── 버튼 ──
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Btn_Leave;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Btn_Start;

	// ── 참가자 슬롯 (고정 4개) ──
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot0_Name;
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot0_Tag;
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot1_Name;
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot1_Tag;
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot2_Name;
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot2_Tag;
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot3_Name;
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Slot3_Tag;

	// 로컬 플레이어 강조용 행 배경 (선택)
	UPROPERTY(meta = (BindWidgetOptional))
	UBorder* Slot0_Row;
	UPROPERTY(meta = (BindWidgetOptional))
	UBorder* Slot1_Row;
	UPROPERTY(meta = (BindWidgetOptional))
	UBorder* Slot2_Row;
	UPROPERTY(meta = (BindWidgetOptional))
	UBorder* Slot3_Row;

private:
	// 슬롯 위젯을 인덱스로 다루기 위한 캐시 (NativeConstruct에서 채움)
	UPROPERTY()
	TArray<UTextBlock*> SlotNames;

	UPROPERTY()
	TArray<UTextBlock*> SlotTags;

	UPROPERTY()
	TArray<UBorder*> SlotRows;

	UPROPERTY()
	UNetSubsystem* Net = nullptr;
};
