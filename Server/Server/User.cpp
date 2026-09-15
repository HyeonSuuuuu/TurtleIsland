#include "pch.h"
#include "User.h"
#include "Globals.h"
#include "ObstacleMap.h"
#include "Database.h"

// -------------------------------------------------------
// 로비 방 목록 푸시 헬퍼
// -------------------------------------------------------
namespace
{
	// 현재 활성 방들로 SC_LOBBY_RESPONSE(가변 길이) 패킷 구성
	std::vector<uint8_t> BuildLobbyRoomListPacket()
	{
		constexpr uint32_t MAX_PER_PACKET =
			(MAX_SOCKBUF - sizeof(PacketHeader) - sizeof(int16_t)) / sizeof(RoomInfo);

		std::vector<RoomInfo> roomList;
		roomList.reserve(MAX_PER_PACKET);
		for (uint32_t i = 0; i < MAX_ROOMS && roomList.size() < MAX_PER_PACKET; ++i)
			if (g_rooms[i].IsActive())
				roomList.push_back(g_rooms[i].GetRoomInfo());

		const uint32_t roomCount  = static_cast<uint32_t>(roomList.size());
		const uint32_t packetSize = static_cast<uint32_t>(
			sizeof(PacketHeader) + sizeof(int16_t) + sizeof(RoomInfo) * roomCount);

		std::vector<uint8_t> buf(packetSize, 0);
		auto* header = reinterpret_cast<PacketHeader*>(buf.data());
		header->size = static_cast<uint16_t>(packetSize);
		header->id   = static_cast<uint16_t>(PACKET_ID::SC_LOBBY_RESPONSE);
		*reinterpret_cast<int16_t*>(buf.data() + sizeof(PacketHeader)) = static_cast<int16_t>(roomCount);
		if (roomCount > 0)
			CopyMemory(buf.data() + sizeof(PacketHeader) + sizeof(int16_t),
				roomList.data(), sizeof(RoomInfo) * roomCount);
		return buf;
	}

	// 로비 상태인 모든 유저에게 최신 방 목록 푸시 (방 생성/입장/퇴장 시 호출)
	void BroadcastRoomListToLobby()
	{
		std::vector<uint8_t> buf = BuildLobbyRoomListPacket();
		std::lock_guard lock(g_onlineMutex);
		for (auto& [uid, slot] : g_onlineUsers)
		{
			User* u = g_userPool.Get(slot);
			if (u && u->GetUserState() == User::UserState::Lobby)
				u->Send(std::span<const uint8_t>(buf.data(), buf.size()));
		}
	}
}

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
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_CREATE_REQUEST)]= &User::OnRoomCreateRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_LEAVE_REQUEST)] = &User::OnRoomLeaveRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_CHAT_REQUEST)]  = &User::OnRoomChatRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ROOM_START_REQUEST)] = &User::OnRoomStartRequest;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_MOVE)]               = &User::OnMove;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_INGAME_READY)]       = &User::OnIngameReady;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_NPC_CHAT)]           = &User::OnNpcChat;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_ATTACK)] = &User::OnAttack;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_USE_ITEM)] = &User::OnUseItem;
	s_handlers[static_cast<uint16_t>(PACKET_ID::CS_SHOP_BUY)] = &User::OnShopBuy;
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

	// 방 인원 변화/방 소멸이 로비 목록에 반영되도록 푸시
	BroadcastRoomListToLobby();
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

	// DB 인증: 존재하면 비번 검증, 없으면 자동 가입. DB 미연결이면 폴백(통과).
	std::string loginId(pLogin->userID, strnlen(pLogin->userID, MAX_USER_ID_LEN));
	std::string password(pLogin->userPW, strnlen(pLogin->userPW, MAX_USER_PW_LEN));

	uint8_t isNewAccount = 0;
	if (g_db.IsConnected())
	{
		uint64_t accountId = 0;
		AuthResult r = g_db.LoginOrRegister(loginId, password, accountId);
		// Ok = 기존 계정 로그인, Registered = 신규 가입 — 둘 다 성공
		if (r != AuthResult::Ok && r != AuthResult::Registered)
		{
			SC_Login fail{};
			fail.result   = static_cast<uint8_t>(ERROR_CODE::USER_LOGIN_FAIL);
			fail.playerId = 0;
			SendPacket(PACKET_ID::SC_LOGIN_RESPONSE, fail);
			Log::Warning("[Login] 실패 userId={} result={}", loginId, static_cast<int>(r));
			return;
		}
		isNewAccount = (r == AuthResult::Registered) ? 1 : 0;
	}

	// 중복 접속 차단: 같은 ID가 이미 온라인이면 거부
	{
		std::lock_guard lock(g_onlineMutex);
		if (g_onlineUsers.find(loginId) != g_onlineUsers.end())
		{
			SC_Login fail{};
			fail.result   = static_cast<uint8_t>(ERROR_CODE::USER_ALREADY_LOGGED_IN);
			fail.playerId = 0;
			SendPacket(PACKET_ID::SC_LOGIN_RESPONSE, fail);
			Log::Warning("[Login] 중복 접속 차단 userId={}", loginId);
			return;
		}
		g_onlineUsers[loginId] = GetSlotIndex();
	}

	Login(loginId);
	Log::Info("[Login] userId={} slot={} id={} new={}", m_userId, GetSlotIndex(), GetId(), isNewAccount);

	SC_Login response{};
	response.result       = static_cast<uint8_t>(ERROR_CODE::NONE);
	response.playerId     = GetId();
	response.isNewAccount = isNewAccount;
	SendPacket(PACKET_ID::SC_LOGIN_RESPONSE, response);
}

void User::OnLobbyEnterRequest(const Packet& packet)
{
	if (m_userState != UserState::Lobby)
		return;

	std::vector<uint8_t> buf = BuildLobbyRoomListPacket();
	Send(std::span<const uint8_t>(buf.data(), buf.size()));
	Log::Info("[Lobby] userId={} 방 목록 전송", m_userId);
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

	// 입장자에게 '이미 방에 있던' 기존 참가자 목록을 각각 SC_ROOM_ENTER_NOTIFY 로 전송
	// (클라는 이 패킷으로 멤버 목록을 채움 — 자기 자신은 mySlot 제외로 빠짐)
	g_rooms[roomIdx].ForEachUser([this](uint32_t slot)
	{
		User* member = g_userPool.Get(slot);
		if (!member) return;

		SC_RoomEnterNotify info{};
		info.size      = SC_ROOM_ENTER_NOTIFY_PACKET_SIZE;
		info.id        = static_cast<uint16_t>(PACKET_ID::SC_ROOM_ENTER_NOTIFY);
		info.userIndex = member->GetId();
		strncpy_s(info.userID, member->GetUserId().c_str(), MAX_USER_ID_LEN);

		Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&info), SC_ROOM_ENTER_NOTIFY_PACKET_SIZE));
	}, mySlot);

	Log::Info("[RoomEnter] userId={} roomIdx={} result={}", m_userId, roomIdx, static_cast<int>(result));

	// 인원 수가 바뀌었으니 로비 유저들에게 방 목록 푸시
	BroadcastRoomListToLobby();
}

void User::OnRoomCreateRequest(const Packet& packet)
{
	if (m_userState != UserState::Lobby)
		return;

	if (packet.dataSpan.size() < CS_ROOM_CREATE_PACKET_SIZE)
		return;

	auto* pReq = reinterpret_cast<const CS_RoomCreate*>(packet.dataSpan.data());
	std::string_view title(pReq->roomTitle,
		strnlen(pReq->roomTitle, MAX_ROOM_TITLE_LEN));

	// 같은 제목의 활성 방이 있으면 거부 (제목 중복 금지)
	if (!title.empty())
	{
		for (uint32_t i = 0; i < MAX_ROOMS; ++i)
		{
			if (!g_rooms[i].IsActive()) continue;
			RoomInfo info = g_rooms[i].GetRoomInfo();
			std::string_view existing(info.roomTitle, strnlen(info.roomTitle, MAX_ROOM_TITLE_LEN));
			if (title == existing)
			{
				SC_RoomEnter dup{};
				dup.result = static_cast<uint8_t>(ERROR_CODE::ROOM_TITLE_DUP);
				SendPacket(PACKET_ID::SC_ROOM_ENTER_RESPONSE, dup);
				Log::Warning("[RoomCreate] 제목 중복 거부 title={}", std::string(title));
				return;
			}
		}
	}

	// Find the first free room slot
	uint32_t roomIdx = MAX_ROOMS;
	for (uint32_t i = 0; i < MAX_ROOMS; ++i)
	{
		if (!g_rooms[i].IsActive())
		{
			roomIdx = i;
			break;
		}
	}

	if (roomIdx >= MAX_ROOMS)
	{
		// No free room
		SC_RoomEnter response{};
		response.result = static_cast<uint8_t>(ERROR_CODE::USER_LOGIN_USED_ALL);
		SendPacket(PACKET_ID::SC_ROOM_ENTER_RESPONSE, response);
		return;
	}

	uint32_t mySlot = GetSlotIndex();
	RoomEnterResult result = g_rooms[roomIdx].Enter(
		static_cast<uint16_t>(roomIdx), mySlot, m_userId, title);

	if (result != RoomEnterResult::Created)
	{
		SC_RoomEnter response{};
		response.result = static_cast<uint8_t>(ERROR_CODE::USER_LOGIN_USED_ALL);
		SendPacket(PACKET_ID::SC_ROOM_ENTER_RESPONSE, response);
		return;
	}

	// Success — same response as room enter so the client transitions into the room
	m_roomIndex = roomIdx;
	m_userState = UserState::Room;

	SC_RoomEnter response{};
	response.result = static_cast<uint8_t>(ERROR_CODE::NONE);
	SendPacket(PACKET_ID::SC_ROOM_ENTER_RESPONSE, response);

	Log::Info("[RoomCreate] userId={} roomIdx={} title={}", m_userId, roomIdx, std::string(title));

	// 로비 유저들에게 새 방 목록 푸시(실시간 갱신)
	BroadcastRoomListToLobby();
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

	room.BroadcastNpcSpawns();   // 게임 시작 시 스폰된 NPC 통지

	Log::Info("[RoomStart] roomIdx={} hostUserId={}", roomIdx, m_userId);
}

void User::OnMove(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex.has_value())
		return;

	if (packet.dataSpan.size() != CS_MOVE_PACKET_SIZE)
		return;

	auto* pMove  = reinterpret_cast<const CS_Move*>(packet.dataSpan.data());
	g_rooms[m_roomIndex.value()].MovePlayer(*this, *pMove);
}

void User::OnIngameReady(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex.has_value())
		return;

	if (packet.dataSpan.size() != CS_INGAME_READY_PACKET_SIZE) return;
	g_rooms[m_roomIndex.value()].PlayerReady(*this, *reinterpret_cast<const CS_IngameReady*>(packet.dataSpan.data()));
}

void User::OnAttack(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex || packet.dataSpan.size() != CS_ATTACK_PACKET_SIZE) return;
	g_rooms[*m_roomIndex].Attack(*this, *reinterpret_cast<const CS_Attack*>(packet.dataSpan.data()));
}

void User::OnUseItem(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex || packet.dataSpan.size() != CS_USE_ITEM_PACKET_SIZE) return;
	g_rooms[*m_roomIndex].UseItem(*this, *reinterpret_cast<const CS_UseItem*>(packet.dataSpan.data()));
}

void User::OnShopBuy(const Packet& packet)
{
	if (m_userState != UserState::Room || !m_roomIndex || packet.dataSpan.size() != CS_SHOP_BUY_PACKET_SIZE) return;
	g_rooms[*m_roomIndex].BuyItem(*this, *reinterpret_cast<const CS_ShopBuy*>(packet.dataSpan.data()));
}

void User::OnNpcChat(const Packet& packet)
{
	if (m_userState != UserState::Room)
		return;

	if (packet.dataSpan.size() < CS_NPC_CHAT_PACKET_SIZE)
		return;

	auto* pNpcChat = reinterpret_cast<const CS_NpcChat*>(packet.dataSpan.data());
	const uint8_t* textPtr = packet.dataSpan.data() + sizeof(CS_NpcChat);
	size_t textLen  = packet.dataSpan.size() - sizeof(CS_NpcChat);
	if (!m_roomIndex || *m_roomIndex>=g_rooms.size()) return;
	g_rooms[*m_roomIndex].BeginNpcChat(*this,pNpcChat->npcIndex,
		std::string(reinterpret_cast<const char*>(textPtr),textLen));
}
