#include "pch.h"
#include "User.h"
#include "Globals.h"

// -------------------------------------------------------
// Lifecycle
// -------------------------------------------------------

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

bool User::RegisterSocket(HANDLE iocpHandle, SOCKET socket, uint32_t slotIndex)
{
	bool ret = Session::RegisterSocket(iocpHandle, socket, slotIndex);
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
	m_userState = UserState::Lobby;
	m_userId    = userId;
}

// -------------------------------------------------------
// Dispatch
// -------------------------------------------------------

void User::RegisterHandlers()
{
	if (!s_handlers.empty()) return;

	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_LOGIN_REQUEST)]      = &User::OnLoginRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_LOBBY_ENTER_REQUEST)]= &User::OnLobbyEnterRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_ENTER_REQUEST)] = &User::OnRoomEnterRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_LEAVE_REQUEST)] = &User::OnRoomLeaveRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_CHAT_REQUEST)]  = &User::OnRoomChatRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_START_REQUEST)] = &User::OnRoomStartRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_MOVE)]               = &User::OnMove;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_NPC_CHAT)]           = &User::OnNpcChat;
}

void User::Dispatch(const Packet& packet)
{
	auto it = s_handlers.find(packet.packetId);
	if (it != s_handlers.end())
		(this->*it->second)(packet);
	else
		Log::Warning("Unknown packet ID: {}", packet.packetId);
}

// -------------------------------------------------------
// Helper
// -------------------------------------------------------

void User::LeaveCurrentRoom()
{
	if (!m_roomIndex.has_value()) return;

	uint32_t roomIdx = m_roomIndex.value();
	Room& room = g_rooms[roomIdx];

	bool empty = room.Leave(GetSlotIndex());
	m_roomIndex = std::nullopt;
	m_userState = UserState::Lobby;

	if (!empty)
	{
		// Notify remaining members
		SC_RoomLeaveNotify notify{};
		notify.size      = SC_ROOM_LEAVE_NOTIFY_PACKET_SIZE;
		notify.id        = static_cast<uint16_t>(PACKET_ID::SC_ROOM_LEAVE_NOTIFY);
		notify.userIndex = GetId(); // playerId

		room.Broadcast(std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(&notify), SC_ROOM_LEAVE_NOTIFY_PACKET_SIZE));
	}
}

// -------------------------------------------------------
// Handlers
// -------------------------------------------------------

void User::OnLoginRequest(const Packet& packet)
{
	if (m_userState != UserState::Login)
	{
		Log::Warning("[Login] Already logged in (Id:{})", GetId());
		return;
	}

	if (packet.dataSpan.size() < CS_LOGIN_PACKET_SIZE)
		return;

	auto* pLogin = reinterpret_cast<const CS_Login*>(packet.dataSpan.data());

	// TODO: DB authentication
	Login(pLogin->userID);
	Log::Info("[Login] userId={} slot={} id={}", m_userId, GetSlotIndex(), GetId());

	SC_Login response{};
	response.result   = static_cast<uint8_t>(ERROR_CODE::NONE);
	response.playerId = GetId();
	SendPacket(PACKET_ID::SC_LOGIN_RESPONSE, response);
}

void User::OnLobbyEnterRequest(const Packet& packet)
{
	if (m_userState != UserState::Lobby)
		return;

	// Collect room list (cap at what fits in one packet)
	constexpr uint32_t MAX_PER_PACKET =
		(MAX_SOCKBUF - sizeof(PacketHeader) - sizeof(int16_t)) / sizeof(RoomInfo);

	std::vector<RoomInfo> roomList;
	roomList.reserve(MAX_PER_PACKET);

	for (uint32_t i = 0; i < MAX_ROOMS && roomList.size() < MAX_PER_PACKET; ++i)
	{
		if (g_rooms[i].IsActive())
			roomList.push_back(g_rooms[i].GetRoomInfo());
	}

	// Build variable-length packet manually
	const uint32_t roomCount   = static_cast<uint32_t>(roomList.size());
	const uint32_t packetSize  = static_cast<uint32_t>(
		sizeof(PacketHeader) + sizeof(int16_t) + sizeof(RoomInfo) * roomCount);

	std::vector<uint8_t> buf(packetSize, 0);
	auto* header         = reinterpret_cast<PacketHeader*>(buf.data());
	header->size         = static_cast<uint16_t>(packetSize);
	header->id           = static_cast<uint16_t>(PACKET_ID::SC_LOBBY_RESPONSE);

	auto* countPtr       = reinterpret_cast<int16_t*>(buf.data() + sizeof(PacketHeader));
	*countPtr            = static_cast<int16_t>(roomCount);

	if (roomCount > 0)
	{
		CopyMemory(buf.data() + sizeof(PacketHeader) + sizeof(int16_t),
			roomList.data(), sizeof(RoomInfo) * roomCount);
	}

	Send(std::span<const uint8_t>(buf.data(), buf.size()));
	Log::Info("[Lobby] userId={} roomCount={}", m_userId, roomCount);
}

void User::OnRoomEnterRequest(const Packet& packet)
{
	if (m_userState != UserState::Lobby)
		return;

	if (packet.dataSpan.size() < CS_ROOM_ENTER_PACKET_SIZE)
		return;

	auto* pReq = reinterpret_cast<const CS_RoomEnter*>(packet.dataSpan.data());
	uint32_t roomIdx = pReq->roomIndex;

	if (roomIdx >= MAX_ROOMS)
	{
		SC_RoomEnter response{};
		response.result = static_cast<uint8_t>(ERROR_CODE::USER_LOGIN_USED_ALL);
		SendPacket(PACKET_ID::SC_ROOM_ENTER_RESPONSE, response);
		return;
	}

	uint32_t mySlot = GetSlotIndex();
	RoomEnterResult result = g_rooms[roomIdx].Enter(
		static_cast<uint16_t>(roomIdx), mySlot, m_userId);

	if (result == RoomEnterResult::Full || result == RoomEnterResult::InGame)
	{
		SC_RoomEnter response{};
		response.result = static_cast<uint8_t>(ERROR_CODE::USER_LOGIN_USED_ALL);
		SendPacket(PACKET_ID::SC_ROOM_ENTER_RESPONSE, response);
		return;
	}

	// Success
	m_roomIndex = roomIdx;
	m_userState = UserState::Room;

	SC_RoomEnter response{};
	response.result = static_cast<uint8_t>(ERROR_CODE::NONE);
	SendPacket(PACKET_ID::SC_ROOM_ENTER_RESPONSE, response);

	// Notify others in the room
	SC_RoomEnterNotify notify{};
	notify.size      = SC_ROOM_ENTER_NOTIFY_PACKET_SIZE;
	notify.id        = static_cast<uint16_t>(PACKET_ID::SC_ROOM_ENTER_NOTIFY);
	notify.userIndex = GetId(); // playerId
	strncpy_s(notify.userID, m_userId.c_str(), MAX_USER_ID_LEN);

	g_rooms[roomIdx].Broadcast(
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&notify), SC_ROOM_ENTER_NOTIFY_PACKET_SIZE),
		mySlot);

	Log::Info("[RoomEnter] userId={} roomIdx={} result={}", m_userId, roomIdx, static_cast<int>(result));
}

void User::OnRoomLeaveRequest(const Packet& packet)
{
	if (m_userState != UserState::Room)
		return;

	LeaveCurrentRoom();
	Log::Info("[RoomLeave] userId={}", m_userId);
}

void User::OnRoomChatRequest(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex.has_value())
		return;

	if (packet.dataSpan.size() < CS_ROOM_CHAT_PACKET_SIZE)
		return;

	auto* pChat = reinterpret_cast<const CS_RoomChat*>(packet.dataSpan.data());
	uint32_t roomIdx = m_roomIndex.value();

	// Ack to sender
	SC_RoomChatResponse ack{};
	ack.result = static_cast<uint8_t>(ERROR_CODE::NONE);
	SendPacket(PACKET_ID::SC_ROOM_CHAT_RESPONSE, ack);

	// Broadcast to room
	SC_RoomChatNotify notify{};
	notify.size      = SC_ROOM_CHAT_NOTIFY_PACKET_SIZE;
	notify.id        = static_cast<uint16_t>(PACKET_ID::SC_ROOM_CHAT_NOTIFY);
	notify.userIndex = GetId();
	strncpy_s(notify.chatMsg, pChat->chatMsg, MAX_CHAT_MSG_LEN);

	g_rooms[roomIdx].Broadcast(
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&notify), SC_ROOM_CHAT_NOTIFY_PACKET_SIZE));
}

void User::OnRoomStartRequest(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex.has_value())
		return;

	uint32_t roomIdx = m_roomIndex.value();
	Room& room = g_rooms[roomIdx];

	// Only host can start
	if (room.GetHostSlot() != packet.sessionIndex)
		return;

	room.StartGame();

	SC_RoomStart notify{};
	notify.size   = SC_ROOM_START_PACKET_SIZE;
	notify.id     = static_cast<uint16_t>(PACKET_ID::SC_ROOM_START_NOTIFY);
	notify.result = static_cast<uint8_t>(ERROR_CODE::NONE);

	room.Broadcast(
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&notify), SC_ROOM_START_PACKET_SIZE));

	Log::Info("[RoomStart] roomIdx={} hostUserId={}", roomIdx, m_userId);
}

void User::OnMove(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex.has_value())
		return;

	if (packet.dataSpan.size() < CS_MOVE_PACKET_SIZE)
		return;

	auto* pMove  = reinterpret_cast<const CS_Move*>(packet.dataSpan.data());
	uint32_t roomIdx = m_roomIndex.value();

	// Simple server-side position update (no physics validation for now)
	constexpr float SPEED = 5.0f; // units/tick
	float rad = pMove->yaw * 3.14159265f / 180.0f;
	uint16_t flags = pMove->inputFlag;
	if (flags & 0x01) { x += std::cos(rad) * SPEED; z += std::sin(rad) * SPEED; } // forward
	if (flags & 0x02) { x -= std::cos(rad) * SPEED; z -= std::sin(rad) * SPEED; } // backward
	if (flags & 0x04) { x -= std::sin(rad) * SPEED; z += std::cos(rad) * SPEED; } // left
	if (flags & 0x08) { x += std::sin(rad) * SPEED; z -= std::cos(rad) * SPEED; } // right
	yaw = pMove->yaw;

	SC_MoveNotify notify{};
	notify.size      = SC_MOVE_NOTIFY_PACKET_SIZE;
	notify.id        = static_cast<uint16_t>(PACKET_ID::SC_MOVE_NOTIFY);
	notify.userIndex = GetId();
	notify.inputFlag = pMove->inputFlag;
	notify.x         = x;
	notify.y         = y;
	notify.z         = z;
	notify.yaw       = pMove->yaw;

	g_rooms[roomIdx].Broadcast(
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&notify), SC_MOVE_NOTIFY_PACKET_SIZE),
		packet.sessionIndex); // exclude sender
}

void User::OnNpcChat(const Packet& packet)
{
	if (m_userState != UserState::Room)
		return;

	if (packet.dataSpan.size() < CS_NPC_CHAT_PACKET_SIZE)
		return;

	if (!g_aiSession)
	{
		Log::Warning("[NpcChat] AI server not connected");
		return;
	}

	auto* pNpcChat = reinterpret_cast<const CS_NpcChat*>(packet.dataSpan.data());
	const uint8_t* textPtr = packet.dataSpan.data() + sizeof(CS_NpcChat);
	size_t textLen  = packet.dataSpan.size() - sizeof(CS_NpcChat);

	// Forward to AI server as SA_AI + variable text
	const size_t totalSize = sizeof(SA_AI) + textLen;
	if (totalSize > MAX_SOCKBUF)
	{
		Log::Warning("[NpcChat] Packet too large ({})", totalSize);
		return;
	}

	std::vector<uint8_t> buf(totalSize, 0);
	auto* pFwd       = reinterpret_cast<SA_AI*>(buf.data());
	pFwd->size       = static_cast<uint16_t>(totalSize);
	pFwd->id         = static_cast<uint16_t>(PACKET_ID::SA_AI);
	pFwd->userIndex  = GetId();      // playerId so AI can echo it back
	pFwd->npcIndex   = pNpcChat->npcIndex;

	if (textLen > 0)
		CopyMemory(buf.data() + sizeof(SA_AI), textPtr, textLen);

	g_aiSession->Send(std::span<const uint8_t>(buf.data(), buf.size()));
	Log::Info("[NpcChat] userId={} npcIndex={} textLen={}", m_userId, pNpcChat->npcIndex, textLen);
}
