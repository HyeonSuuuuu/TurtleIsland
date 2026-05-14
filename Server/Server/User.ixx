module;
#include <unordered_map>
export module User;
import Common;
import Define;
import Session;
import <windows.h>;
import "../../Common/Protocol.h";

export class User : public Session
{
public:
	User() = default;
	virtual ~User() = default;

	enum class UserState : uint8
	{
		Login = 0,
		Lobby = 1,
		Room  = 2,
	};

	virtual void Init(uint32 id) override
	{
		Session::Init(id);
	}

	virtual void InitClear() override
	{
		m_userState = UserState::Login;
		m_userId = "";
		m_roomIndex = std::nullopt;
		Session::InitClear();
	}

	virtual bool RegisterSocket(HANDLE iocpHandle, SOCKET socket) override
	{
		bool ret = Session::RegisterSocket(iocpHandle, socket);
		if (ret)
		{
			state.store(SessionState::Connected);
			m_userState = UserState::Login;
			m_userId = "";
			m_roomIndex = std::nullopt;
		}
		return ret;
	}

	virtual void Close(bool isForce = false) override
	{
		Session::Close(isForce);
	}

	void Login(std::string_view userId)
	{
		m_userState = UserState::Login;
		m_userId = userId;
	}

	std::string GetUserId() const { return m_userId; }
	UserState GetUserState() const { return m_userState; }

	using PacketHandler = void(User::*)(const Packet&);
	static inline std::unordered_map<uint16, PacketHandler> s_handlers;
	static void RegisterHandlers()
	{
		if (!s_handlers.empty()) return;
	}

	virtual void Dispatch(const Packet& packet) override
	{
		auto it = s_handlers.find(packet.packetId);
		if (it != s_handlers.end())
			(this->*it->second)(packet);
		else
			Log::Warning("Unknown packet ID: {}", packet.packetId);
	}

	std::mutex m_stateLock;

private:
	std::optional<uint32> m_roomIndex;
	std::string m_userId;
	UserState m_userState = UserState::Login;
};
