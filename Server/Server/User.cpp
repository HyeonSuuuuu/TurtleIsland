#include "pch.h"
#include "User.h"

void User::Init(uint32_t id)
{
	Session::Init(id);
}

void User::Clear()
{
	m_userState = UserState::Login;
	m_userId    = "";
	m_roomIndex = std::nullopt;
	Session::Clear();
}

bool User::RegisterSocket(HANDLE iocpHandle, SOCKET socket)
{
	bool ret = Session::RegisterSocket(iocpHandle, socket);
	if (ret)
	{
		m_userState = UserState::Login;
		m_userId    = "";
		m_roomIndex = std::nullopt;
	}
	return ret;
}

void User::Close(bool isForce)
{
	Session::Close(isForce);
}

void User::Login(std::string_view userId)
{
	m_userState = UserState::Login;
	m_userId    = userId;
}

void User::RegisterHandlers()
{
	if (!s_handlers.empty()) return;
}

void User::Dispatch(const Packet& packet)
{
	auto it = s_handlers.find(packet.packetId);
	if (it != s_handlers.end())
		(this->*it->second)(packet);
	else
		Log::Warning("Unknown packet ID: {}", packet.packetId);
}
