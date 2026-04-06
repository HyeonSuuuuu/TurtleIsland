export module PacketManager;

import Common;
import "../../Common/Protocol.h";
import UserManager;
import Define;

export class PacketManager
{
public:
	PacketManager() = delete;
	~PacketManager() = default;

	PacketManager(const uint32 maxClient)
	{
		Register(PACKET_ID::SYS_USER_CONNECT, &PacketManager::ProcessUserConnect);
		Register(PACKET_ID::SYS_USER_DISCONNECT, &PacketManager::ProcessUserDisConnect);
		Register(PACKET_ID::CS_LOGIN_REQUEST, &PacketManager::ProcessLogin);
		Register(PACKET_ID::CS_MOVE, &PacketManager::ProcessMove);
		Register(PACKET_ID::CS_ROOM_START_REQUEST, &PacketManager::ProcessStartRoom);
		CreateComponent(maxClient);
	}

	// Thread 및 Queue 완전 제거됨 (Direct Processing 방식 변환)

	void ReceivePacket(const uint32 sessionIndex, const std::span<const byte> dataSpan)
	{
		auto pUser = m_userManager->GetUser(sessionIndex);
		pUser->SetPacket(dataSpan);
		
		// 큐에 넣는 대신 Worker Thread가 즉시 패킷을 꺼내서 처리
		while (true)
		{
			if (std::optional<Packet> packetOpt = pUser->GetPacket(); packetOpt.has_value())
			{
				ProcessRecvPacket(packetOpt.value().sessionIndex, packetOpt.value().packetId, packetOpt.value().dataSpan);
			}
			else
			{
				break;
			}
		}
	}
	
	void ProcessSystemPacket(const Packet& packet)
	{
		ProcessRecvPacket(packet.sessionIndex, packet.packetId, packet.dataSpan);
	}

	std::function<void(uint32 clientIndex, std::span<const byte> dataSpan)> m_sendFunc;
private:

	void CreateComponent(const uint32 maxClient)
	{
		m_userManager = std::make_unique<UserManager>(maxClient);
	}

	void ProcessRecvPacket(const uint32 sessionIndex, const uint16 packetId, std::span<const byte> packet)
	{
		if (auto it = m_recvFuncDict.find(packetId); it != m_recvFuncDict.end())
		{
			it->second(sessionIndex, packet);
		}
		else
		{
			Log::Warning("sessionId({}): 알 수 없는 패킷, packetId: {}", sessionIndex, packetId);
		}
	}

	void Register(PACKET_ID id, void(PacketManager::*handler)(uint32, std::span<const byte>))
	{
		m_recvFuncDict[static_cast<uint16>(id)] = [this, handler](uint32 idx, std::span<const byte> data) {
			(this->*handler)(idx, data);
		};
	}

	// --------------------------------- Packet Func -------------------------------------------------------------------

	void ProcessUserConnect(uint32 sessionIndex, std::span<const byte> dataSpan)
	{
		Log::Info("[ProcessUserConnect] sessionIndex: {}", sessionIndex);
		m_userManager->AddUser(sessionIndex);
		
	}

	void ProcessUserDisConnect(uint32 sessionIndex, std::span<const byte> dataSpan)
	{
		Log::Info("[ProcessUserDisConnect] sessionIndex: {}", sessionIndex);
		m_userManager->RemoveUser(sessionIndex);
	}
	
	void ProcessLogin(uint32 sessionIndex, std::span<const byte> dataSpan)
	{
		if (CS_LOGIN_PACKET_SIZE != dataSpan.size())
			return;

		const CS_Login* loginReqPacket = reinterpret_cast<const CS_Login*>(dataSpan.data());
		Log::Info("id: {} pw: {}", loginReqPacket->userID, loginReqPacket->userPW);

		SC_Login loginResPacket{};
		loginResPacket.size = SC_LOGIN_PACKET_SIZE;
		loginResPacket.id = static_cast<uint16>(PACKET_ID::SC_LOGIN_RESPONSE);
		// 아이디, 비번 검사 (DB 연동)

		loginResPacket.result = static_cast<uint16>(ERROR_CODE::NONE);
		loginResPacket.playerId = sessionIndex;
		
		std::span<const byte> sendSpan(reinterpret_cast<const byte*>(&loginResPacket), SC_LOGIN_PACKET_SIZE);
		this->m_sendFunc(sessionIndex, sendSpan);
		
		Log::Info("로그인 응답 전송 완료 - Sessin {}", sessionIndex);
	}

	void ProcessMove(uint32 sessionIndex, std::span<const byte> dataSpan)
	{
		if (CS_MOVE_PACKET_SIZE != dataSpan.size())
			return;

		const CS_Move* movePacket = reinterpret_cast<const CS_Move*>(dataSpan.data());
		Log::Info("CS_Move({}): {}", sessionIndex, movePacket->inputFlag);

		const auto& user = m_userManager->GetUser(sessionIndex);
		// 이동처리 - 멀티스레드 쓰기 락 보호
		{
			std::lock_guard lock(user->m_stateLock);
			if (movePacket->inputFlag & 1) user->x += 10.0f; // W
			if (movePacket->inputFlag & 2) user->y -= 10.0f; // A
			if (movePacket->inputFlag & 4) user->x -= 10.0f; // S
			if (movePacket->inputFlag & 8) user->y += 10.0f; // D
		}

		SC_MoveNotify resPkt{};
		resPkt.id = static_cast<uint16>(PACKET_ID::SC_MOVE_NOTIFY);
		resPkt.size = SC_MOVE_NOTIFY_PACKET_SIZE;
		resPkt.x = user->x;
		resPkt.y = user->y;
		resPkt.z = user->z;
		resPkt.inputFlag = movePacket->inputFlag;
		resPkt.userIndex = sessionIndex;
		std::span<const byte> sendSpan(reinterpret_cast<const byte*>(&resPkt), SC_MOVE_NOTIFY_PACKET_SIZE);
		
		// broadcast - 전체 유저맵 Shared Lock(동시 읽기) 채용
		std::shared_lock lock(m_userManager->m_userMapLock);
		for (uint32 index : m_userManager->m_connectedUserIndex)
		{
			this->m_sendFunc(index, sendSpan);
		}
	}

	void ProcessStartRoom(uint32 sessionIndex, std::span<const byte> dataSpan)
	{
		if (CS_ROOM_START_PACKET_SIZE != dataSpan.size())
			return;
		Log::Info("StartRoom패킷 확인");

		m_userManager->m_connectedUserIndex.push_back(sessionIndex);
		// 성공
		SC_RoomStart roomResPacket{};
		roomResPacket.size = SC_ROOM_START_PACKET_SIZE;
		roomResPacket.id = static_cast<uint16>(PACKET_ID::SC_ROOM_START_NOTIFY);
		roomResPacket.result = static_cast<uint8>(ERROR_CODE::NONE);
		// 같이있는 룸 유저에게 전체 보내야됨 (원래는)
		std::span<const byte> sendSpan(reinterpret_cast<const byte*>(&roomResPacket), SC_ROOM_START_PACKET_SIZE);
		this->m_sendFunc(sessionIndex, sendSpan);

		// 이전에 있었던 사람들에게 위치 알림
		const auto& user = m_userManager->GetUser(sessionIndex);
		SC_MoveNotify resPkt{};
		resPkt.id = static_cast<uint16>(PACKET_ID::SC_MOVE_NOTIFY);
		resPkt.size = SC_MOVE_NOTIFY_PACKET_SIZE;
		resPkt.x = user->x;
		resPkt.y = user->y;
		resPkt.z = user->z;
		resPkt.userIndex = sessionIndex;
		resPkt.inputFlag = 0;
		resPkt.userIndex = sessionIndex;
		std::span<const byte> sendSpan2(reinterpret_cast<const byte*>(&resPkt), SC_MOVE_NOTIFY_PACKET_SIZE);

		for (uint32 index : m_userManager->m_connectedUserIndex)
		{
			if (index == sessionIndex)
				continue;
			this->m_sendFunc(index, sendSpan);
		}
		// 다른 사람의 위치 가져옴
		for (uint32 index : m_userManager->m_connectedUserIndex)
		{
			if (index == sessionIndex)
				continue;
			const auto& otherUser = m_userManager->GetUser(sessionIndex);
			SC_MoveNotify resPkt{};
			resPkt.id = static_cast<uint16>(PACKET_ID::SC_MOVE_NOTIFY);
			resPkt.size = SC_MOVE_NOTIFY_PACKET_SIZE;
			resPkt.x = otherUser->x;
			resPkt.y = otherUser->y;
			resPkt.z = otherUser->z;
			resPkt.inputFlag = 0;
			resPkt.userIndex = index;
			this->m_sendFunc(sessionIndex, sendSpan);
		}
	}

	// AI에게 Send
	void ProcesstNpcChat(uint32 sessionIndex, std::span<const byte> dataSpan)
	{
/*		if (dataSpan.size() < CS_NPC_CHAT_PACKET_SIZE)
			return;
		const CS_NpcChat* npcChatPacket = reinterpret_cast<const CS_NpcChat*>(dataSpan.data());
		if (dataSpan.size() < CS_NPC_CHAT_PACKET_SIZE + sizeof(uint32_t))
			return;
		uint32_t chatLen = *reinterpret_cast<const uint32_t*>(dataSpan.data() + CS_NPC_CHAT_PACKET_SIZE);
		
		if (dataSpan.size() < CS_NPC_CHAT_PACKET_SIZE + sizeof(uint32_t) + chatLen)
		char* chatMsg = reinterpret_cast<char*>(dataSpan.data() + CS_NPC_CHAT_PACKET_SIZE + sizeof(uint32_t));
		Log::Info("Client({}): {}", sessionIndex, chatMsg);*/

	}

	std::function<void(uint32, std::span<const byte>)> m_recvFunc;
	std::unordered_map<uint16, std::function<void(uint32, std::span<const byte>)>> m_recvFuncDict;

	std::unique_ptr<UserManager> m_userManager;
	
	// 큐와 스레드 변수 완전 제거됨

};