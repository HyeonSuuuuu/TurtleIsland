#include "pch.h"
#include "ObstacleMap.h"
#include "Log.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>

bool ObstacleMap::Load(const char* path)
{
	m_loaded = false;
	m_blocked.clear();

	std::ifstream file(path);
	if (!file)
	{
		Log::Warning("[Map] '{}' open fail; movement disabled", path);
		return false;
	}

	// 헤더: cols rows cell originX originY
	int cols = 0, rows = 0, cell = 0;
	long ox = 0, oy = 0;
	if (!(file >> cols >> rows >> cell >> ox >> oy)
		|| cols <= 0 || rows <= 0 || cols > 4096 || rows > 4096 || cell <= 0 || cell > 100000)
	{
		Log::Warning("[Map] '{}' header parse fail", path);
		return false;
	}

	m_cols = cols; m_rows = rows;
	m_cell = static_cast<float>(cell);
	m_ox = static_cast<float>(ox);
	m_oy = static_cast<float>(oy);
	m_blocked.assign(static_cast<size_t>(cols) * rows, 0);

	int filled = 0;
	for (int r = 0; r < rows; ++r)
	{
		std::string row;
		if (!(file >> row) || row.size() != static_cast<size_t>(cols) || row.find_first_not_of("01") != std::string::npos)
		{
			Log::Warning("[Map] '{}' invalid/truncated grid; movement disabled", path);
			return false;
		}
		for (int c = 0; c < cols; ++c)
		{
			m_blocked[static_cast<size_t>(r) * cols + c] = row[c] == '1';
			filled += row[c] == '1';
		}
	}

	m_loaded = true;
	Log::Success("[Map] '{}' loaded {}x{} cell={} origin=({},{}) blocked={}",
		path, m_cols, m_rows, static_cast<int>(m_cell),
		static_cast<int>(m_ox), static_cast<int>(m_oy), filled);
	return true;
}

bool ObstacleMap::IsBlockedWorld(float wx, float wy) const
{
	if (!m_loaded || !std::isfinite(wx) || !std::isfinite(wy)) return true;
	if (wx < m_ox || wy < m_oy || wx >= m_ox + static_cast<double>(m_cols) * m_cell ||
		wy >= m_oy + static_cast<double>(m_rows) * m_cell) return true;

	int c = static_cast<int>(std::floor((wx - m_ox) / m_cell));
	int r = static_cast<int>(std::floor((wy - m_oy) / m_cell));
	if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) return true;  // 그리드 밖 = 막힘
	return BlockedCell(c, r);
}

bool ObstacleMap::CanTraverse(float fromX, float fromY, float toX, float toY, float radius) const
{
	if (!m_loaded || !std::isfinite(fromX) || !std::isfinite(fromY) || !std::isfinite(toX) ||
		!std::isfinite(toY) || !std::isfinite(radius) || radius < 0.f) return false;
	const double minX = std::min(fromX, toX) - static_cast<double>(radius);
	const double minY = std::min(fromY, toY) - static_cast<double>(radius);
	const double maxX = std::max(fromX, toX) + static_cast<double>(radius);
	const double maxY = std::max(fromY, toY) + static_cast<double>(radius);
	if (minX < m_ox || minY < m_oy || maxX >= m_ox + static_cast<double>(m_cols) * m_cell ||
		maxY >= m_oy + static_cast<double>(m_rows) * m_cell) return false;
	const int left = std::max(0, static_cast<int>(std::floor((minX - m_ox) / m_cell)) - 1);
	const int top = std::max(0, static_cast<int>(std::floor((minY - m_oy) / m_cell)) - 1);
	const int right = static_cast<int>(std::floor((maxX - m_ox) / m_cell));
	const int bottom = static_cast<int>(std::floor((maxY - m_oy) / m_cell));
	for (int r = top; r <= bottom; ++r)
		for (int c = left; c <= right; ++c)
		{
			if (!BlockedCell(c, r)) continue;
			double enter = 0., leave = 1.;
			const auto overlapsAxis = [&enter, &leave](double start, double end, double low, double high)
			{
				const double delta = end - start;
				if (delta == 0.) return start >= low && start <= high;
				double a = (low - start) / delta, b = (high - start) / delta;
				if (a > b) std::swap(a, b);
				enter = std::max(enter, a); leave = std::min(leave, b);
				return enter <= leave;
			};
			const double x = m_ox + static_cast<double>(c) * m_cell;
			const double y = m_oy + static_cast<double>(r) * m_cell;
			if (overlapsAxis(fromX, toX, x - radius, x + m_cell + radius) &&
				overlapsAxis(fromY, toY, y - radius, y + m_cell + radius)) return false;
		}
	return true;
}

void ObstacleMap::SnapToFree(float& wx, float& wy, float radius) const
{
	if (!m_loaded || !std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(radius) || radius < 0.f ||
		CanTraverse(wx, wy, wx, wy, radius)) return;

	int c0 = static_cast<int>(std::clamp(std::floor((static_cast<double>(wx) - m_ox) / m_cell), 0., m_cols - 1.));
	int r0 = static_cast<int>(std::clamp(std::floor((static_cast<double>(wy) - m_oy) / m_cell), 0., m_rows - 1.));

	// 동심 사각 테두리를 넓혀가며 가장 가까운 이동가능 칸 탐색
	for (int rad = 0; rad < (m_cols + m_rows); ++rad)
	{
		for (int dr = -rad; dr <= rad; ++dr)
		{
			for (int dc = -rad; dc <= rad; ++dc)
			{
				if (std::abs(dr) != rad && std::abs(dc) != rad) continue; // 테두리만
				int c = c0 + dc, r = r0 + dr;
				if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) continue;
				const float x = m_ox + (c + 0.5f) * m_cell;
				const float y = m_oy + (r + 0.5f) * m_cell;
				if (CanTraverse(x, y, x, y, radius))
				{
					wx = m_ox + (c + 0.5f) * m_cell;
					wy = m_oy + (r + 0.5f) * m_cell;
					return;
				}
			}
		}
	}
}
