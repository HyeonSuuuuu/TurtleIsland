#include "pch.h"
#include "ObstacleMap.h"
#include "Log.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

bool ObstacleMap::Load(const char* path)
{
	m_loaded = false;
	m_blocked.clear();

	FILE* fp = nullptr;
	if (fopen_s(&fp, path, "rb") != 0 || !fp)
	{
		Log::Warning("[Map] '{}' open fail — 장애물 충돌 비활성", path);
		return false;
	}

	// 헤더: cols rows cell originX originY
	int cols = 0, rows = 0, cell = 0;
	long ox = 0, oy = 0;
	if (fscanf_s(fp, "%d %d %d %ld %ld", &cols, &rows, &cell, &ox, &oy) != 5
		|| cols <= 0 || rows <= 0 || cell <= 0)
	{
		Log::Warning("[Map] '{}' header parse fail", path);
		fclose(fp);
		return false;
	}

	m_cols = cols; m_rows = rows;
	m_cell = static_cast<float>(cell);
	m_ox = static_cast<float>(ox);
	m_oy = static_cast<float>(oy);
	m_blocked.assign(static_cast<size_t>(cols) * rows, 0);

	// 본문: '0'/'1' 만 칸으로 사용, 개행으로 행 구분(\r 등은 무시)
	int filled = 0, r = 0, c = 0, ch;
	while (r < rows && (ch = std::fgetc(fp)) != EOF)
	{
		if (ch == '0' || ch == '1')
		{
			if (c < cols)
			{
				if (ch == '1') { m_blocked[(size_t)r * cols + c] = 1; ++filled; }
				++c;
			}
		}
		else if (ch == '\n')
		{
			if (c > 0) { ++r; c = 0; }
		}
	}
	fclose(fp);

	m_loaded = true;
	Log::Success("[Map] '{}' loaded {}x{} cell={} origin=({},{}) blocked={}",
		path, m_cols, m_rows, static_cast<int>(m_cell),
		static_cast<int>(m_ox), static_cast<int>(m_oy), filled);
	return true;
}

bool ObstacleMap::IsBlockedWorld(float wx, float wy) const
{
	if (!m_loaded) return false;   // 미로드 시 충돌 비활성(통과)

	int c = static_cast<int>(std::floor((wx - m_ox) / m_cell));
	int r = static_cast<int>(std::floor((wy - m_oy) / m_cell));
	if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) return true;  // 그리드 밖 = 막힘
	return BlockedCell(c, r);
}

void ObstacleMap::SnapToFree(float& wx, float& wy) const
{
	if (!m_loaded || !IsBlockedWorld(wx, wy)) return;

	int c0 = static_cast<int>(std::floor((wx - m_ox) / m_cell));
	int r0 = static_cast<int>(std::floor((wy - m_oy) / m_cell));

	// 동심 사각 테두리를 넓혀가며 가장 가까운 이동가능 칸 탐색
	for (int rad = 1; rad < (m_cols + m_rows); ++rad)
	{
		for (int dr = -rad; dr <= rad; ++dr)
		{
			for (int dc = -rad; dc <= rad; ++dc)
			{
				if (std::abs(dr) != rad && std::abs(dc) != rad) continue; // 테두리만
				int c = c0 + dc, r = r0 + dr;
				if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) continue;
				if (!BlockedCell(c, r))
				{
					wx = m_ox + (c + 0.5f) * m_cell;
					wy = m_oy + (r + 0.5f) * m_cell;
					return;
				}
			}
		}
	}
}
