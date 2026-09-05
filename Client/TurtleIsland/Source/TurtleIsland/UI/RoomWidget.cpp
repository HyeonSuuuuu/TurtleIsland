// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/RoomWidget.h"
#include "Network/NetSubsystem.h"

#include "GameFramework/PlayerController.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"

namespace RoomUITheme
{
	// 슬롯의 동적 상태 색상 (기본 룩은 디자이너에서 지정, 코드는 상태 변화만 반영)
	static const FLinearColor Accent(0.16f, 0.78f, 0.86f, 1.0f);    // 시안 강조
	static const FLinearColor RowEmpty(1.f, 1.f, 1.f, 0.04f);
	static const FLinearColor RowFilled(1.f, 1.f, 1.f, 0.08f);
	static const FLinearColor RowLocal(0.16f, 0.78f, 0.86f, 0.16f);
	static const FLinearColor EmptyText(0.50f, 0.58f, 0.65f, 1.0f);
}

void URoomWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 메뉴 화면이므로 마우스 커서를 표시하고 UI 입력 모드로 전환
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
	}

	if (UGameInstance* GI = GetGameInstance())
		Net = GI->GetSubsystem<UNetSubsystem>();

	// 슬롯 위젯을 인덱스 배열로 캐시 (디자이너에 없으면 null로 채워져도 안전)
	SlotNames = { Slot0_Name, Slot1_Name, Slot2_Name, Slot3_Name };
	SlotTags  = { Slot0_Tag,  Slot1_Tag,  Slot2_Tag,  Slot3_Tag };
	SlotRows  = { Slot0_Row,  Slot1_Row,  Slot2_Row,  Slot3_Row };

	if (Btn_Leave)
		Btn_Leave->OnClicked.AddDynamic(this, &URoomWidget::OnLeaveClicked);
	if (Btn_Start)
		Btn_Start->OnClicked.AddDynamic(this, &URoomWidget::OnStartClicked);

	if (Net)
		Net->OnRoomMembersUpdated.AddDynamic(this, &URoomWidget::HandleMembersUpdated);

	RefreshMembers();
}

void URoomWidget::NativeDestruct()
{
	if (Net)
		Net->OnRoomMembersUpdated.RemoveDynamic(this, &URoomWidget::HandleMembersUpdated);

	Super::NativeDestruct();
}

void URoomWidget::RefreshMembers()
{
	const TArray<FRoomMember>* Members = Net ? &Net->RoomMembers : nullptr;
	const int32 Count = Members ? Members->Num() : 0;

	if (Text_Title)
	{
		const FString Title = (Net && !Net->CurrentRoomTitle.IsEmpty())
			? Net->CurrentRoomTitle : TEXT("대기실");
		Text_Title->SetText(FText::FromString(Title));
	}

	if (Text_Host)
	{
		const FString Host = Net ? Net->CurrentRoomHost : FString();
		Text_Host->SetText(Host.IsEmpty()
			? FText::GetEmpty()
			: FText::FromString(FString::Printf(TEXT("방장 · %s"), *Host)));
	}

	if (Text_Count)
	{
		Text_Count->SetText(FText::FromString(
			FString::Printf(TEXT("%d/%d"), Count, UNetSubsystem::MaxRoomMembers)));
	}

	bool bLocalIsHost = false;

	for (int32 i = 0; i < SlotNames.Num(); ++i)
	{
		const bool bFilled = Members && i < Members->Num();

		if (bFilled)
		{
			const FRoomMember& M = (*Members)[i];

			if (SlotNames[i])
			{
				SlotNames[i]->SetText(FText::FromString(M.UserID));
				SlotNames[i]->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			}

			if (SlotTags[i])
			{
				const FString TagStr = M.bIsHost ? TEXT("방장") : (M.bIsLocal ? TEXT("나") : FString());
				SlotTags[i]->SetText(FText::FromString(TagStr));
			}

			if (SlotRows[i])
				SlotRows[i]->SetBrushColor(M.bIsLocal ? RoomUITheme::RowLocal : RoomUITheme::RowFilled);

			if (M.bIsLocal && M.bIsHost)
				bLocalIsHost = true;
		}
		else
		{
			if (SlotNames[i])
			{
				SlotNames[i]->SetText(FText::FromString(TEXT("비어 있음")));
				SlotNames[i]->SetColorAndOpacity(FSlateColor(RoomUITheme::EmptyText));
			}

			if (SlotTags[i])
				SlotTags[i]->SetText(FText::GetEmpty());

			if (SlotRows[i])
				SlotRows[i]->SetBrushColor(RoomUITheme::RowEmpty);
		}
	}

	// 방장이거나 방장 정보를 알 수 없으면(방 생성 직후 등) 시작 버튼 노출
	if (Btn_Start)
	{
		const bool bShowStart = bLocalIsHost || (Net && Net->CurrentRoomHost.IsEmpty());
		Btn_Start->SetVisibility(bShowStart
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void URoomWidget::HandleMembersUpdated()
{
	RefreshMembers();
}

void URoomWidget::OnLeaveClicked()
{
	if (Net)
		Net->LeaveRoom();
}

void URoomWidget::OnStartClicked()
{
	if (Net)
		Net->StartRoom();
}
