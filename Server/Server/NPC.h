#pragma once
#include "pch.h"
#include "GameObject.h"
#include "Define.h"

// NPC 상태(연속좌표 AI). 그리드 A* 없이 추격/복귀만.
enum class NpcState : uint8_t { Idle, Chase, Return };

// NPC : GameObject — 방(Room) 인스턴스가 보유. 좌표는 2D x,y.
class NPC : public GameObject
{
public:
	void Clear() override
	{
		m_active     = false;
		m_hp         = 0;
		m_maxHp      = 0;
		m_type       = 0;
		m_state      = NpcState::Idle;
		m_targetSlot = INVALID_SESSION_INDEX;
		m_speed      = 0.f;
		m_attackReadyAt = 0;
	}

	void Spawn(uint32_t npcId, uint16_t type, float spawnX, float spawnY, int32_t hp, float speed)
	{
		Init(npcId);
		x = spawnX; y = spawnY; yaw = 0.f;
		m_anchorX = spawnX; m_anchorY = spawnY;
		m_type    = type;
		m_hp      = hp;
		m_maxHp   = hp;
		m_speed   = speed;
		m_state   = NpcState::Idle;
		m_targetSlot = INVALID_SESSION_INDEX;
		m_active  = true;
		m_attackReadyAt = 0;
	}

	bool IsActive() const { return m_active; }
	bool IsAlive()  const { return m_hp > 0; }

	bool     m_active = false;
	uint16_t m_type   = 0;
	int32_t  m_hp     = 0;
	int32_t  m_maxHp  = 0;
	NpcState m_state  = NpcState::Idle;
	uint32_t m_targetSlot = INVALID_SESSION_INDEX;
	float    m_anchorX = 0.f, m_anchorY = 0.f;
	float    m_speed   = 0.f;   // units/tick (MonsterDef)
	uint64_t m_attackReadyAt = 0;
};
