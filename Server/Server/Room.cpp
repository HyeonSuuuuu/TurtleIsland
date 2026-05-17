#include "pch.h"
#include "Room.h"
#include "Globals.h"

RoomEnterResult Room::Enter(uint16_t roomId, uint32_t slotIndex, std::string_view userName)
{
	std::lock_guard lock(m_mutex);

	if (!m_isActive)
	{
		// Create new room
		m_roomId   = roomId;
		m_isActive = true;
		m_inGame   = false;
		m_userCount = 0;
		m_userSlots.fill(INVALID_SESSION_INDEX);

		std::string title = std::string(userName) + "'s Room";
		strncpy_s(m_title, title.c_str(), MAX_ROOM_TITLE_LEN - 1);
		m_title[MAX_ROOM_TITLE_LEN - 1] = '\0';

		SetHost(slotIndex, userName);
		m_userSlots[m_userCount++] = slotIndex;
		return RoomEnterResult::Created;
	}

	if (m_inGame)
		return RoomEnterResult::InGame;

	if (m_userCount >= MAX_ROOM_USERS)
		return RoomEnterResult::Full;

	m_userSlots[m_userCount++] = slotIndex;
	return RoomEnterResult::Joined;
}

bool Room::Leave(uint32_t slotIndex)
{
	std::lock_guard lock(m_mutex);

	// Find and remove
	bool found = false;
	for (uint32_t i = 0; i < m_userCount; ++i)
	{
		if (m_userSlots[i] == slotIndex)
		{
			for (uint32_t j = i; j < m_userCount - 1; ++j)
				m_userSlots[j] = m_userSlots[j + 1];
			m_userSlots[--m_userCount] = INVALID_SESSION_INDEX;
			found = true;
			break;
		}
	}

	if (!found) return false;

	if (m_userCount == 0)
	{
		m_isActive = false;
		m_inGame   = false;
		return true;
	}

	// Promote new host if host left
	if (m_hostSlot == slotIndex)
	{
		m_hostSlot = m_userSlots[0];
		User* newHost = g_userPool.Get(m_userSlots[0]);
		if (newHost)
		{
			strncpy_s(m_hostName, newHost->GetUserId().c_str(), MAX_USER_ID_LEN - 1);
			m_hostName[MAX_USER_ID_LEN - 1] = '\0';
		}
	}
	return false;
}

void Room::Broadcast(const std::span<const uint8_t> data, uint32_t excludeSlot)
{
	std::lock_guard lock(m_mutex);
	for (uint32_t i = 0; i < m_userCount; ++i)
	{
		uint32_t slot = m_userSlots[i];
		if (slot == excludeSlot) continue;
		User* pUser = g_userPool.Get(slot);
		if (pUser) pUser->Send(data);
	}
}

void Room::StartGame()
{
	std::lock_guard lock(m_mutex);
	m_inGame = true;
}

RoomInfo Room::GetRoomInfo() const
{
	std::lock_guard lock(m_mutex);
	RoomInfo info{};
	info.roomId    = m_roomId;
	info.userCount = static_cast<uint16_t>(m_userCount);
	strncpy_s(info.roomTitle, m_title,    MAX_ROOM_TITLE_LEN - 1);
	strncpy_s(info.hostName,  m_hostName, MAX_USER_ID_LEN - 1);
	return info;
}

void Room::SetHost(uint32_t slotIndex, std::string_view userName)
{
	m_hostSlot = slotIndex;
	strncpy_s(m_hostName, userName.data(), MAX_USER_ID_LEN - 1);
	m_hostName[MAX_USER_ID_LEN - 1] = '\0';
}
