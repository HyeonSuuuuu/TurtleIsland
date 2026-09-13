#include "pch.h"
#include "ObstacleMap.h"
#include "Log.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <atomic>
#include <filesystem>
#include "../../Common/ThirdParty/nlohmann/json.hpp"

bool MapSpawn::LoadPlayerStart(const char* path)
{
	try
	{
		std::ifstream file(path);
		const auto data = nlohmann::json::parse(file);
		for (const char* key : {"x", "y", "yaw", "ground_z"})
			if (!data.contains(key) || !data[key].is_number() || !std::isfinite(data[key].get<double>()) ||
				std::abs(data[key].get<double>()) > 10000000.) return false;
		PLAYER_X = data["x"].get<float>(); PLAYER_Y = data["y"].get<float>();
		PLAYER_YAW = std::remainder(data["yaw"].get<float>(), 360.f);
		PLAYER_GROUND_Z = data["ground_z"].get<float>();
		NPC_ANCHOR_X = PLAYER_X - 20.f; NPC_ANCHOR_Y = PLAYER_Y + 1600.f;
		Log::Success("[Map] PlayerStart loaded ({},{}) yaw={}", PLAYER_X, PLAYER_Y, PLAYER_YAW);
		return true;
	}
	catch (const std::exception&) { return false; }
}

bool ObstacleMap::Load(const char* path)
{
	m_loaded = false;
	m_mode = Mode::Grid;
	m_shapes = ShapeCollisionMap{};
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
	if (m_mode != Mode::Grid) return !CanTraverse(wx, wy, wx, wy);
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
	if (!m_loaded) return false;
	if (m_mode == Mode::Shapes) return m_shapes.CanTraverse(fromX, fromY, toX, toY, radius);
	const bool grid = CanTraverseGrid(fromX, fromY, toX, toY, radius);
	if (m_mode == Mode::Shadow)
	{
		const bool shapes = m_shapes.CanTraverse(fromX, fromY, toX, toY, radius);
		static std::atomic<uint64_t> differences = 0;
		if (grid != shapes)
		{
			const auto count = ++differences;
			if (count <= 20 || count % 1000 == 0)
				Log::Warning("[Collision] difference #{} grid={} shapes={} ({},{}) -> ({},{}) r={}",
					count, grid, shapes, fromX, fromY, toX, toY, radius);
		}
	}
	return grid;
}

bool ObstacleMap::CanTraverseGrid(float fromX, float fromY, float toX, float toY, float radius) const
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
	if (m_mode == Mode::Shapes) { m_shapes.SnapToFree(wx, wy, radius); return; }
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

bool ObstacleMap::LoadConfigured(const char* settingsPath)
{
	*this = ObstacleMap{};
	try
	{
		std::ifstream file(settingsPath);
		const auto config = nlohmann::json::parse(file);
		const auto base = std::filesystem::path(settingsPath).parent_path();
		const auto mode = config.at("mode").get<std::string>();
		if (mode != "grid" && mode != "shadow" && mode != "shapes")
			throw std::runtime_error("Unknown collision mode");
		if (mode != "shapes")
		{
			if (!MapSpawn::LoadPlayerStart((base / "PlayerSpawn.json").string().c_str()) ||
				!Load((base / "map.dat").string().c_str()))
				throw std::runtime_error("Missing or invalid legacy map/spawn");
		}
		if (mode != "grid")
		{
			const auto scene = (base / config.at("scene").get<std::string>()).string();
			if (!m_shapes.Load(scene.c_str())) throw std::runtime_error(m_shapes.LastError());
			if (mode == "shadow" && (std::abs(m_shapes.SpawnX() - MapSpawn::PLAYER_X) > 0.1f ||
				std::abs(m_shapes.SpawnY() - MapSpawn::PLAYER_Y) > 0.1f))
				throw std::runtime_error("Shadow scene and legacy PlayerStart do not match");
			if (mode == "shapes")
			{
				MapSpawn::PLAYER_X = m_shapes.SpawnX(); MapSpawn::PLAYER_Y = m_shapes.SpawnY();
				MapSpawn::PLAYER_GROUND_Z = m_shapes.SpawnGroundZ();
				MapSpawn::PLAYER_YAW = std::remainder(m_shapes.SpawnYaw(), 360.f);
				MapSpawn::NPC_ANCHOR_X = MapSpawn::PLAYER_X - 20.f;
				MapSpawn::NPC_ANCHOR_Y = MapSpawn::PLAYER_Y + 1600.f;
			}
		}
		m_mode = mode == "shapes" ? Mode::Shapes : mode == "shadow" ? Mode::Shadow : Mode::Grid;
		m_loaded = true;
		Log::Success("[Collision] mode={} polygons={} PlayerStart=({},{})", mode,
			m_shapes.PolygonCount(), MapSpawn::PLAYER_X, MapSpawn::PLAYER_Y);
		return true;
	}
	catch (const std::exception& e)
	{
		*this = ObstacleMap{};
		Log::Warning("[Collision] {}: {}; gameplay movement disabled", settingsPath, e.what());
		return false;
	}
}
