#pragma once
#include "pch.h"
#include <vector>
#include <cstdint>

// 2D 장애물 그리드 (Unreal 월드좌표 기준). map.dat 로부터 로드.
//   헤더 1줄: cols rows cell originX originY
//   이후 rows 줄: '0'=이동가능, '1'=막힘
//   worldToGrid: col = floor((wx-originX)/cell), row = floor((wy-originY)/cell)
class ObstacleMap
{
public:
	bool Load(const char* path);
	bool IsLoaded() const { return m_loaded; }

	// 월드(x,y) 가 막힌 칸인지. 미로드 시 false(통과), 그리드 밖은 true(아레나 이탈 방지).
	bool IsBlockedWorld(float wx, float wy) const;
	bool CanMoveTo(float wx, float wy) const { return !IsBlockedWorld(wx, wy); }

	// (x,y) 가 막힌 칸이면 가장 가까운 이동가능 칸 중심으로 보정. 스폰 안전망.
	void SnapToFree(float& wx, float& wy) const;

private:
	bool BlockedCell(int c, int r) const { return m_blocked[(size_t)r * m_cols + c] != 0; }

	int     m_cols = 0, m_rows = 0;
	float   m_cell = 100.f, m_ox = 0.f, m_oy = 0.f;
	bool    m_loaded = false;
	std::vector<uint8_t> m_blocked;  // row-major (m_cols * m_rows)
};

inline ObstacleMap g_obstacleMap;

// 인게임(Lv3_Ingame) 플레이어 스폰 월드좌표 — Unreal PlayerStart 와 일치
namespace MapSpawn
{
	constexpr float PLAYER_X = -24730.f;
	constexpr float PLAYER_Y = -24750.f;
	// NPC 스폰 앵커 — 스폰 지점에서 이어지는 좌측 개방 통로 중앙(개방 칸)
	constexpr float NPC_ANCHOR_X = -24750.f;
	constexpr float NPC_ANCHOR_Y = -23150.f;
}
