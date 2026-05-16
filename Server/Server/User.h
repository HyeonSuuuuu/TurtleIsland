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
	virtual bool RegisterSocket(HANDLE iocpHandle, SOCKET socket) override;
	virtual void Close(bool isForce = false) override;

	void Login(std::string_view userId);

	std::string GetUserId()    const { return m_userId; }
	UserState   GetUserState() const { return m_userState; }

	using PacketHandler = void(User::*)(const Packet&);
	static inline std::unordered_map<uint16_t, PacketHandler> s_handlers;
	static void RegisterHandlers();

	virtual void Dispatch(const Packet& packet) override;

private:
	std::optional<uint32_t> m_roomIndex;
	std::string             m_userId;
	std::atomic<UserState>  m_userState = UserState::Login;
};
