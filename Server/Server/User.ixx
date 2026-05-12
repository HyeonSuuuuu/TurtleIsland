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

	enum class State
	{
		None      = 0,
		Connected = 1,
		Login     = 2,
		Lobby     = 3,
		Room      = 4,
	};

	virtual void Init(uint32 id) override
	{
		Session::Init(id);
		m_state = State::None;
		m_userId = "";
		m_roomIndex = std::nullopt;
	}

	virtual void Clear() override
	{
		Session::Clear();
		m_state = State::None;
		m_userId = "";
		m_roomIndex = std::nullopt;
	}

	virtual bool RegisterSocket(HANDLE iocpHandle, SOCKET socket) override
	{
		bool ret = Session::RegisterSocket(iocpHandle, socket);
		if (ret)
		{
			m_state = State::Connected;
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
		m_state = State::Login;
		m_userId = userId;
	}

	std::string GetUserId() const { return m_userId; }
	State GetState() const { return m_state; }

	// ----------------------------------------------------
	// Packet Dispatcher
	// ----------------------------------------------------

	using PacketHandler = void(User::*)(const Packet&);
	static inline std::unordered_map<uint16, PacketHandler> s_handlers; // 삽입, 삭제 x
	static void RegisterHandlers()
	{
		if (!s_handlers.empty()) return;

		// s_handlers[CS_LOGIN_REQ] = &User::HandleLogin;
	}

	virtual void Dispatch(const Packet& packet) override
	{
		auto it = s_handlers.find(packet.packetId);
		if (it != s_handlers.end())
			(this->*it->second)(packet);
		else
			Log::Warning("알 수 없는 패킷입니다: ID {}", packet.packetId);
	}

	// ----------------------------------------------------

	std::mutex m_stateLock; // state 락 안걸어도 될듯 고민해봐야됨

private:
	std::optional<uint32> m_roomIndex;
	std::string m_userId;
	State m_state = State::None;
};
