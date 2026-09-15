#pragma once
#include "pch.h"
#include "Session.h"
#include "../../Common/Protocol.h"

class User : public Session
{
public:
	User() = default;
	virtual ~User() = default;

	enum class UserState : uint8_t
	{
		Login = 0,
		Lobby = 1,
		Room  = 2,
	};

	virtual void Init(uint32_t id) override;
	virtual void Clear() override;
	virtual bool RegisterSocket(HANDLE iocpHandle, SOCKET socket, uint32_t slotIndex) override;
	virtual void Close(bool isForce = false) override;

	void Login(std::string_view userId);
	void LeaveCurrentRoom(); // called by GameServer::OnClose on disconnect

	std::string GetUserId()    const { return m_userId; }
	UserState   GetUserState() const { return m_userState.load(); }

	using PacketHandler = void(User::*)(const Packet&);
	static inline std::unordered_map<uint16_t, PacketHandler> s_handlers;
	static void RegisterHandlers();

	virtual void Dispatch(const Packet& packet) override;

private:
	// ---------- Packet Handlers ----------
	void OnLoginRequest(const Packet& packet);
	void OnLobbyEnterRequest(const Packet& packet);
	void OnRoomEnterRequest(const Packet& packet);
	void OnRoomCreateRequest(const Packet& packet);
	void OnRoomLeaveRequest(const Packet& packet);
	void OnRoomChatRequest(const Packet& packet);
	void OnRoomStartRequest(const Packet& packet);
	void OnMove(const Packet& packet);
	void OnIngameReady(const Packet& packet);
	void OnNpcChat(const Packet& packet);
	void OnNpcChatEnd(const Packet& packet);
	void OnAttack(const Packet& packet);
	void OnUseItem(const Packet& packet);
	void OnShopBuy(const Packet& packet);

	// ---------- Helpers ----------
	template<typename T>
	void SendPacket(PACKET_ID id, T& pkt)
	{
		pkt.size = sizeof(T);
		pkt.id   = static_cast<uint16_t>(id);
		Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&pkt), sizeof(T)));
	}

	std::optional<uint32_t> m_roomIndex;
	std::string             m_userId;
	std::atomic<UserState>  m_userState{ UserState::Login };
};
