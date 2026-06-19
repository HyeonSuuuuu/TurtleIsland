#pragma once
#include "pch.h"
#include "IOCPNetwork.h"

// ===================== GameServer =======================
// 콘텐츠: IOCPNetwork 상속. 현재는 프레임워크 골격(On* 최소 구현).
// 콘텐츠 단계에서 Room/AI/NPC 디스패치를 여기 채운다.
class GameServer : public IOCPNetwork
{
public:
	GameServer() = default;
	virtual ~GameServer() = default;

	virtual void OnConnect(const uint32_t slotIndex) override;
	virtual void OnClose(const uint32_t slotIndex) override;
	virtual void OnRecv(uint32_t slotIndex, const Packet& packet) override;

	void Run();
	void Stop();

private:
	void OnAiRecv(const Packet& packet);   // AI 서버 응답(AS_AI) → 해당 유저에게 SC_NpcChat
};
