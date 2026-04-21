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
	User() = delete;
	User(uint32 index)
		: Session(index)
	{}
	virtual ~User() = default;

	enum class DOMAIN_STATE
	{
		NONE = 0,
		CONNECTED = 1,
		LOGIN = 2,
		LOBBY = 3,
		ROOM = 4,
	};

	void Init()
	{
		m_domainState = DOMAIN_STATE::CONNECTED;

		m_userId = "";
		m_roomIndex = std::nullopt;
	}

	void ClearUserData()
	{
		m_roomIndex = std::nullopt;
		m_userId = "";
		m_domainState = DOMAIN_STATE::NONE;
	}

	int SetLogin(std::string_view userId)
	{
		m_domainState = DOMAIN_STATE::LOGIN;
		m_userId = userId;
		return 0;
	}





	std::string GetUserId() const
	{
		return m_userId;
	}

	DOMAIN_STATE GetDomainState() const
	{
		return m_domainState;
	}



	virtual bool OnConnect(HANDLE iocpHandle, SOCKET socket) override
	{
		bool ret = Session::OnConnect(iocpHandle, socket);
		if (ret)
		{
			Init(); // Initialize User state when connected
		}
		return ret;
	}

	virtual void Close(bool isForce = false) override
	{
		Session::Close(isForce);
		ClearUserData();
	}

	// ----------------------------------------------------
	// Packet Dispatcher 관련 구현
	// ----------------------------------------------------

	using PacketHandler = void(User::*)(const Packet&);
	static inline std::unordered_map<uint16, PacketHandler> s_handlers;

	static void InitHandlers()
	{
		if (!s_handlers.empty()) return;

		// 예시: s_handlers[CS_LOGIN_REQ] = &User::HandleLogin;
		// s_handlers[PKT_CS_LOGIN] = &User::HandleLogin; // 실제 패킷 ID에 맵핑
	}

	void ProcessPacket(const Packet& packet)
	{
		auto it = s_handlers.find(packet.packetId);
		if (it != s_handlers.end())
		{
			PacketHandler handlerFunc = it->second;
			(this->*handlerFunc)(packet); // O(1) 함수 실행
		}
		else
		{
			Log::Warning("알 수 없는 패킷입니다: ID {}", packet.packetId);
		}
	}
	// ----------------------------------------------------

	std::mutex m_stateLock;
	float x{}, y{}, z{ 88 };

private:
	std::optional<uint32> m_roomIndex;
	std::string m_userId;
	DOMAIN_STATE m_domainState = DOMAIN_STATE::NONE;
};
