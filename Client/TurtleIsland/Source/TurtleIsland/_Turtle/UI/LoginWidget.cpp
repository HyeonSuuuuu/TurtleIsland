// Fill out your copyright notice in the Description page of Project Settings.

#include "LoginWidget.h"
#include "NetSubsystem.h"
#include "Protocol.h"
#include "ServerSettingsWidget.h"

void ULoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (LoginButton)
		LoginButton->OnClicked.AddDynamic(this, &ULoginWidget::OnLoginButtonClicked);
	if (SettingsButton)
		SettingsButton->OnClicked.AddDynamic(this, &ULoginWidget::OnSettingsButtonClicked);

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
		Net->OnLoginFailed.AddDynamic(this, &ULoginWidget::ShowError);
}

void ULoginWidget::OnSettingsButtonClicked()
{
	if (!SettingsPopupClass) return;

	if (ActiveSettingsPopup)
	{
		ActiveSettingsPopup->RemoveFromParent();
		ActiveSettingsPopup = nullptr;
	}

	ActiveSettingsPopup = CreateWidget<UServerSettingsWidget>(GetOwningPlayer(), SettingsPopupClass);
	if (ActiveSettingsPopup)
	{
		UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
		if (Net)
			ActiveSettingsPopup->InitWithIP(Net->GetServerIP());

		ActiveSettingsPopup->OnIPSaved.AddDynamic(this, &ULoginWidget::OnServerIPSaved);
		ActiveSettingsPopup->AddToViewport(10);
	}
}

void ULoginWidget::OnServerIPSaved(FString NewIP)
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
		Net->SetServerIP(NewIP);

	ActiveSettingsPopup = nullptr;
}

void ULoginWidget::NativeDestruct()
{
	GetWorld()->GetTimerManager().ClearTimer(LoginRetryHandle);

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
		Net->OnLoginFailed.RemoveDynamic(this, &ULoginWidget::ShowError);

	Super::NativeDestruct();
}

void ULoginWidget::ShowError(FString Message)
{
	if (!ErrorPopupClass) return;

	if (ActivePopup)
	{
		ActivePopup->RemoveFromParent();
		ActivePopup = nullptr;
	}

	ActivePopup = CreateWidget<UErrorPopupWidget>(GetOwningPlayer(), ErrorPopupClass);
	if (ActivePopup)
	{
		ActivePopup->SetMessage(TEXT("오류"), Message);
		ActivePopup->AddToViewport(10);
	}
}

void ULoginWidget::OnLoginButtonClicked()
{
	if (ActivePopup)
	{
		ActivePopup->RemoveFromParent();
		ActivePopup = nullptr;
	}

	PendingID = UserIDInput ? UserIDInput->GetText().ToString() : TEXT("");
	PendingPW = UserPWInput ? UserPWInput->GetText().ToString() : TEXT("");

	if (PendingID.TrimStartAndEnd().IsEmpty() || PendingPW.TrimStartAndEnd().IsEmpty())
	{
		ShowError(TEXT("ID 또는 PW를 입력해주세요."));
		return;
	}

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (!Net) return;

	if (!Net->IsConnected())
	{
		Net->Reconnect(Net->GetServerIP());
		// 연결 완료까지 0.5초 대기 후 재시도
		GetWorld()->GetTimerManager().SetTimer(
			LoginRetryHandle, this, &ULoginWidget::TrySendLogin, 0.5f, false);
		return;
	}

	TrySendLogin();
}

void ULoginWidget::TrySendLogin()
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (!Net) return;

	if (!Net->IsConnected())
	{
		ShowError(TEXT("서버에 연결할 수 없습니다."));
		return;
	}

	CS_Login LoginPacket{};
	LoginPacket.size = CS_LOGIN_PACKET_SIZE;
	LoginPacket.id = (uint16)PACKET_ID::CS_LOGIN_REQUEST;
	Net->SetUserID(PendingID);
	Net->CopyStringToBuffer(LoginPacket.userID, MAX_USER_ID_LEN + 1, PendingID);
	Net->CopyStringToBuffer(LoginPacket.userPW, MAX_USER_PW_LEN + 1, PendingPW);

	TArray<uint8> SendBuffer;
	SendBuffer.SetNumUninitialized(sizeof(CS_Login));
	FMemory::Memcpy(SendBuffer.GetData(), &LoginPacket, sizeof(CS_Login));

	Net->EnqueSendPacket(MoveTemp(SendBuffer));
	UE_LOG(LogTemp, Log, TEXT("[Login] 패킷 전송"));
}
