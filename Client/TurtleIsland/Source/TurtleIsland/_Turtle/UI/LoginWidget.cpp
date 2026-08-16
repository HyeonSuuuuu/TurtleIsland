// Fill out your copyright notice in the Description page of Project Settings.

#include "LoginWidget.h"
#include "NetSubsystem.h"
#include "Protocol.h"
#include "ServerSettingsWidget.h"
#include "GameFramework/PlayerController.h"

void ULoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 메뉴 화면이므로 마우스 커서를 표시하고 UI 입력 모드로 전환
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
	}

	if (LoginButton)
		LoginButton->OnClicked.AddDynamic(this, &ULoginWidget::OnLoginButtonClicked);
	if (SettingsButton)
		SettingsButton->OnClicked.AddDynamic(this, &ULoginWidget::OnSettingsButtonClicked);
	if (SinglePlayButton)
		SinglePlayButton->OnClicked.AddDynamic(this, &ULoginWidget::OnSinglePlayButtonClicked);

	// 엔터 입력 시 로그인 시도 (ID/PW 칸 어느 쪽에서 눌러도)
	if (UserIDInput)
		UserIDInput->OnTextCommitted.AddDynamic(this, &ULoginWidget::OnLoginTextCommitted);
	if (UserPWInput)
		UserPWInput->OnTextCommitted.AddDynamic(this, &ULoginWidget::OnLoginTextCommitted);

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
	{
		Net->OnLoginFailed.AddDynamic(this, &ULoginWidget::ShowError);
		Net->OnLoginSucceeded.AddDynamic(this, &ULoginWidget::OnLoginSucceeded);

		// 로그인 화면으로 돌아온 경우 싱글 상태를 초기화
		Net->StartMultiPlay();
	}
}

void ULoginWidget::OnSinglePlayButtonClicked()
{
	if (ActivePopup)
	{
		ActivePopup->RemoveFromParent();
		ActivePopup = nullptr;
	}

	if (UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>())
		Net->StartSinglePlay();
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
	{
		Net->OnLoginFailed.RemoveDynamic(this, &ULoginWidget::ShowError);
		Net->OnLoginSucceeded.RemoveDynamic(this, &ULoginWidget::OnLoginSucceeded);
	}

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

void ULoginWidget::OnLoginSucceeded(bool bIsNewAccount)
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (!Net) return;

	// 신규 가입이면 안내 팝업을 띄우고, 확인을 눌러야 로비로 진입(레벨 전환에 팝업이 사라지지 않게)
	if (bIsNewAccount && ErrorPopupClass)
	{
		if (ActivePopup)
		{
			ActivePopup->RemoveFromParent();
			ActivePopup = nullptr;
		}

		ActivePopup = CreateWidget<UErrorPopupWidget>(GetOwningPlayer(), ErrorPopupClass);
		if (ActivePopup)
		{
			ActivePopup->SetMessage(TEXT("환영합니다"), TEXT("계정이 생성되었습니다."));
			ActivePopup->OnConfirmed.AddDynamic(this, &ULoginWidget::OnAccountCreatedConfirmed);
			ActivePopup->AddToViewport(10);
			return;
		}
	}

	// 기존 계정이거나 팝업 클래스 미설정 → 바로 로비 진입
	Net->EnterLobby();
}

void ULoginWidget::OnAccountCreatedConfirmed()
{
	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
		Net->EnterLobby();
}

void ULoginWidget::OnLoginTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 엔터로 커밋된 경우에만 로그인 시도(포커스 이동/클릭아웃은 무시)
	if (CommitMethod == ETextCommit::OnEnter)
		OnLoginButtonClicked();
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
