// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/LoginWidget.h"
#include "Network/NetSubsystem.h"
#include "Protocol.h"
#include "UI/ServerSettingsWidget.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

void ULoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 메뉴 화면이므로 마우스 커서를 표시하고 UI 입력 모드로 전환
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
	}

	if (SinglePlayButton)
		SinglePlayButton->OnClicked.AddDynamic(this, &ULoginWidget::OnSinglePlayButtonClicked);
	if (MultiPlayButton)
		MultiPlayButton->OnClicked.AddDynamic(this, &ULoginWidget::OnMultiPlayButtonClicked);
	// OptionButton은 아직 동작이 없어 클릭을 연결하지 않는다
	if (ExitButton)
		ExitButton->OnClicked.AddDynamic(this, &ULoginWidget::OnExitButtonClicked);

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
	{
		Net->OnLoginFailed.AddDynamic(this, &ULoginWidget::ShowError);
		Net->OnLoginSucceeded.AddDynamic(this, &ULoginWidget::OnLoginSucceeded);

		// 로그인 화면으로 돌아온 경우 싱글 상태를 초기화
		Net->StartMultiPlay();
	}
}

void ULoginWidget::NativeDestruct()
{
	GetWorld()->GetTimerManager().ClearTimer(LoginRetryHandle);

	CloseLoginPopup();

	UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>();
	if (Net)
	{
		Net->OnLoginFailed.RemoveDynamic(this, &ULoginWidget::ShowError);
		Net->OnLoginSucceeded.RemoveDynamic(this, &ULoginWidget::OnLoginSucceeded);
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------- 메뉴 동작

void ULoginWidget::OnSinglePlayButtonClicked()
{
	if (ActivePopup)
	{
		ActivePopup->RemoveFromParent();
		ActivePopup = nullptr;
	}

	CloseLoginPopup();

	if (UNetSubsystem* Net = GetGameInstance()->GetSubsystem<UNetSubsystem>())
		Net->StartSinglePlay();
}

void ULoginWidget::OnMultiPlayButtonClicked()
{
	if (!LoginPopupClass) return;

	// 이미 떠 있으면 다시 만들지 않는다
	if (ActiveLoginPopup) return;

	ActiveLoginPopup = CreateWidget<ULoginPopupWidget>(GetOwningPlayer(), LoginPopupClass);
	if (ActiveLoginPopup)
	{
		ActiveLoginPopup->OnLoginSubmitted.AddDynamic(this, &ULoginWidget::OnLoginSubmitted);
		ActiveLoginPopup->OnCancelled.AddDynamic(this, &ULoginWidget::OnLoginCancelled);
		ActiveLoginPopup->OnSettingsRequested.AddDynamic(this, &ULoginWidget::OpenServerSettings);
		// 오류 팝업(10)보다 아래에 깔리도록
		ActiveLoginPopup->AddToViewport(5);

		SetMenuInteractable(false);
	}
}

void ULoginWidget::OnLoginCancelled()
{
	// 팝업이 스스로 RemoveFromParent 하므로 참조만 정리
	ActiveLoginPopup = nullptr;
	SetMenuInteractable(true);
}

void ULoginWidget::CloseLoginPopup()
{
	if (ActiveLoginPopup)
	{
		ActiveLoginPopup->RemoveFromParent();
		ActiveLoginPopup = nullptr;
	}

	SetMenuInteractable(true);
}

void ULoginWidget::SetMenuInteractable(bool bEnabled)
{
	if (SinglePlayButton) SinglePlayButton->SetIsEnabled(bEnabled);
	if (MultiPlayButton)  MultiPlayButton->SetIsEnabled(bEnabled);
	if (OptionButton)     OptionButton->SetIsEnabled(bEnabled);
	if (ExitButton)       ExitButton->SetIsEnabled(bEnabled);
}

void ULoginWidget::OnExitButtonClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void ULoginWidget::OpenServerSettings()
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

// ---------------------------------------------------------------- 로그인

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

	// 로그인에 성공했으면 입력 팝업은 닫는다
	CloseLoginPopup();

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

void ULoginWidget::OnLoginSubmitted(FString ID, FString PW)
{
	if (ActivePopup)
	{
		ActivePopup->RemoveFromParent();
		ActivePopup = nullptr;
	}

	PendingID = ID;
	PendingPW = PW;

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

	Net->SendPacket(LoginPacket);
	UE_LOG(LogTemp, Log, TEXT("[Login] 패킷 전송"));
}
