#include "pch.h"
#include "ShapeCollisionMap.h"
#include "../../Common/ThirdParty/box2d/include/box2d/collision.h"
#include "../../Common/ThirdParty/nlohmann/json.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdexcept>

namespace
{
    using Json = nlohmann::json;
    constexpr float CmToMeters = 0.01f;
    constexpr float ContactTolerance = 0.001f; // 0.1 cm, not another actor-radius expansion.

    bool IsConvex(const Json& vertices)
    {
        const int count = static_cast<int>(vertices.size());
        double x[8]{}, y[8]{};
        for (int i = 0; i < count; ++i)
        {
            x[i] = vertices[i][0].get<double>();
            y[i] = vertices[i][1].get<double>();
        }
        double area = 0;
        for (int i = 1; i + 1 < count; ++i)
            area += (x[i]-x[0])*(y[i+1]-y[0]) - (y[i]-y[0])*(x[i+1]-x[0]);
        if (std::abs(area) < 1.e-6) return false;
        const double winding = area > 0 ? 1. : -1.;
        for (int i = 0; i < count; ++i)
        {
            const int next = (i + 1) % count;
            const double dx = x[next]-x[i], dy = y[next]-y[i];
            if (std::hypot(dx,dy) < 1.e-6) return false;
            for (int j = 0; j < count; ++j)
                if (winding * (dx*(y[j]-y[i]) - dy*(x[j]-x[i])) < -1.e-6) return false;
        }
        return true;
    }

    double Number(const Json& value)
    {
        if (!value.is_number()) throw std::runtime_error("Expected numeric coordinate");
        const double v = value.get<double>();
        if (!std::isfinite(v) || std::abs(v) > 10000000.) throw std::runtime_error("Invalid coordinate");
        return v;
    }

    int Integer(const Json& value, int min, int max)
    {
        const double v = Number(value);
        if (v != std::floor(v) || v < min || v > max) throw std::runtime_error("Invalid grid size");
        return static_cast<int>(v);
    }

    bool Overlaps(const b2ShapeProxy& polygon, b2Vec2 point, float radius)
    {
        b2DistanceInput input{};
        input.proxyA = polygon;
        input.proxyB = b2MakeProxy(&point, 1, 0.f);
        input.transformA = input.transformB = b2Transform_identity;
        b2SimplexCache cache{};
        const float distance = b2ShapeDistance(&input, &cache, nullptr, 0).distance;
        return distance <= 0.f || distance < radius - ContactTolerance;
    }

    bool Crosses(const b2ShapeProxy& polygon, b2Vec2 from, b2Vec2 to, float radius)
    {
        // Shape casts do not report initial overlap, so check both endpoints explicitly.
        if (Overlaps(polygon, from, radius) || Overlaps(polygon, to, radius)) return true;
        const b2Vec2 delta = b2Sub(to, from);
        if (b2LengthSquared(delta) < 1.e-12f) return false;
        b2ShapeCastPairInput input{};
        input.proxyA = polygon;
        input.proxyB = b2MakeProxy(&from, 1, radius);
        input.transformA = input.transformB = b2Transform_identity;
        input.translationB = delta;
        input.maxFraction = 1.f;
        input.canEncroach = true;
        const auto hit = b2ShapeCast(&input);
        return hit.hit && hit.fraction < 1.f && b2Dot(hit.normal, delta) < -1.e-7f;
    }
}

struct ShapeCollisionMap::Data
{
    int cols = 0, rows = 0;
    float cell = 0, ox = 0, oy = 0;
    float spawnX = 0, spawnY = 0, spawnYaw = 0;
    std::vector<std::string> terrain;
    std::vector<b2ShapeProxy> polygons;
    b2DynamicTree tree = b2DynamicTree_Create();
    ~Data() { b2DynamicTree_Destroy(&tree); }

    b2Vec2 Point(float x, float y) const { return {(x - ox) * CmToMeters, (y - oy) * CmToMeters}; }
};

bool ShapeCollisionMap::Load(const char* path)
{
    m_data.reset();
    m_error.clear();
    try
    {
        if (std::filesystem::file_size(path) > 64 * 1024 * 1024) throw std::runtime_error("Scene too large");
        std::ifstream stream(path);
        const auto root = Json::parse(stream);
        if (root.at("schema") != 1 || root.at("units") != "cm" || root.at("complete") != true ||
            !root.at("errors").is_array() || !root.at("errors").empty() ||
            !root.at("source_level").is_string() || root.at("source_level").get<std::string>().empty())
            throw std::runtime_error("Incomplete or unsupported collision export");
        auto data = std::make_shared<Data>();
        const auto& terrain = root.at("terrain");
        data->cols = Integer(terrain.at("cols"), 1, 4096);
        data->rows = Integer(terrain.at("rows"), 1, 4096);
        data->cell = static_cast<float>(Number(terrain.at("cell")));
        if (data->cell < 10.f || data->cell > 1000.f || static_cast<int64_t>(data->cols) * data->rows > 4000000)
            throw std::runtime_error("Invalid terrain resolution");
        const auto& origin = terrain.at("origin");
        if (!origin.is_array() || origin.size() != 2) throw std::runtime_error("Invalid origin");
        data->ox = static_cast<float>(Number(origin[0])); data->oy = static_cast<float>(Number(origin[1]));
        const auto& rows = terrain.at("blocked");
        if (!rows.is_array() || rows.size() != data->rows) throw std::runtime_error("Truncated terrain");
        bool hasGround = false;
        for (const auto& row : rows)
        {
            auto text = row.get<std::string>();
            if (text.size() != data->cols || text.find_first_not_of("01") != std::string::npos)
                throw std::runtime_error("Invalid terrain row");
            hasGround |= text.find('0') != std::string::npos;
            data->terrain.push_back(std::move(text));
        }
        if (!hasGround) throw std::runtime_error("No traversable ground");
        const auto& spawn = root.at("spawn");
        data->spawnX = static_cast<float>(Number(spawn.at("x")));
        data->spawnY = static_cast<float>(Number(spawn.at("y")));
        data->spawnYaw = static_cast<float>(Number(spawn.at("yaw")));
        Number(spawn.at("z")); Number(spawn.at("ground_z"));
        const auto& shapes = root.at("polygons");
        if (!shapes.is_array() || shapes.size() > 200000) throw std::runtime_error("Invalid polygon count");
        for (const auto& shape : shapes)
        {
            const auto& vertices = shape.at("vertices");
            if (!vertices.is_array() || vertices.size() < 3 || vertices.size() > B2_MAX_POLYGON_VERTICES)
                throw std::runtime_error("Polygon must have 3..8 convex vertices");
            b2Vec2 points[B2_MAX_POLYGON_VERTICES]{};
            int count = 0;
            for (const auto& v : vertices)
            {
                if (!v.is_array() || v.size() != 2) throw std::runtime_error("Invalid vertex");
                points[count++] = data->Point(static_cast<float>(Number(v[0])), static_cast<float>(Number(v[1])));
            }
            if (!IsConvex(vertices))
                throw std::runtime_error("Invalid polygon #" + std::to_string(data->polygons.size()) + " (" +
                    shape.value("source", std::string("unknown")) + "): concave or degenerate; export convex parts");
            // Query proxies use the same GJK path as b2ShapeCastPolygon, without
            // the rigid-body hull builder's aggressive 2 cm vertex welding.
            const auto polygon = b2MakeProxy(points, count, 0.f);
            b2AABB bounds{points[0], points[0]};
            for (int i = 1; i < count; ++i)
            {
                bounds.lowerBound = b2Min(bounds.lowerBound, points[i]);
                bounds.upperBound = b2Max(bounds.upperBound, points[i]);
            }
            b2DynamicTree_CreateProxy(&data->tree, bounds, 1, data->polygons.size());
            data->polygons.push_back(polygon);
        }
        m_data = std::move(data);
        float x = SpawnX(), y = SpawnY();
        if (!SnapToFree(x, y, 42.f) || std::hypot(x - SpawnX(), y - SpawnY()) > 150.f)
            throw std::runtime_error("PlayerStart has no nearby capsule-safe ground");
        return true;
    }
    catch (const std::exception& e)
    {
        m_data.reset(); m_error = e.what(); return false;
    }
}

bool ShapeCollisionMap::CanTraverse(float fromX, float fromY, float toX, float toY, float radius) const
{
    if (!m_data || !std::isfinite(fromX) || !std::isfinite(fromY) || !std::isfinite(toX) ||
        !std::isfinite(toY) || !std::isfinite(radius) || radius < 0.f || radius > 10000.f) return false;
    const auto& data = *m_data;
    const double left = std::min(fromX, toX) - static_cast<double>(radius);
    const double bottom = std::min(fromY, toY) - static_cast<double>(radius);
    const double right = std::max(fromX, toX) + static_cast<double>(radius);
    const double top = std::max(fromY, toY) + static_cast<double>(radius);
    if (left < data.ox || bottom < data.oy || right >= data.ox + static_cast<double>(data.cols) * data.cell ||
        top >= data.oy + static_cast<double>(data.rows) * data.cell) return false;
    const b2Vec2 from = data.Point(fromX, fromY), to = data.Point(toX, toY);
    const float r = radius * CmToMeters;
    struct Query
    {
        const Data& data;
        b2Vec2 from, to;
        float radius;
        bool blocked = false;
    } query{data, from, to, r};
    b2DynamicTree_Query(&data.tree,
        {{static_cast<float>(left - data.ox) * CmToMeters, static_cast<float>(bottom - data.oy) * CmToMeters},
         {static_cast<float>(right - data.ox) * CmToMeters, static_cast<float>(top - data.oy) * CmToMeters}},
        1, [](int, uint64_t index, void* context)
        {
            auto& q = *static_cast<Query*>(context);
            q.blocked = Crosses(q.data.polygons[index], q.from, q.to, q.radius);
            return !q.blocked;
        }, &query);
    if (query.blocked) return false;
    const int c0 = std::max(0, static_cast<int>((left - data.ox) / data.cell) - 1);
    const int r0 = std::max(0, static_cast<int>((bottom - data.oy) / data.cell) - 1);
    const int c1 = static_cast<int>((right - data.ox) / data.cell);
    const int r1 = static_cast<int>((top - data.oy) / data.cell);
    const float halfCell = data.cell * CmToMeters * 0.5f;
    for (int row = r0; row <= r1; ++row)
        for (int col = c0; col <= c1; ++col)
        {
            if (data.terrain[row][col] != '1') continue;
            const auto cell = b2MakeOffsetBox(halfCell, halfCell,
                {(2 * col + 1) * halfCell, (2 * row + 1) * halfCell}, b2Rot_identity);
            if (Crosses(b2MakeProxy(cell.vertices, cell.count, 0.f), from, to, r)) return false;
        }
    return true;
}

bool ShapeCollisionMap::SnapToFree(float& x, float& y, float radius) const
{
    if (!m_data || !std::isfinite(x) || !std::isfinite(y)) return false;
    if (CanTraverse(x, y, x, y, radius)) return true;
    // Bounded local search avoids silently spawning on the other side of the island.
    const auto& d = *m_data;
    const float step = std::min(25.f, d.cell);
    for (int ring = 1; ring <= 6; ++ring)
        for (int dy = -ring; dy <= ring; ++dy)
            for (int dx = -ring; dx <= ring; ++dx)
            {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                if (std::hypot(dx * step, dy * step) > 150.f) continue;
                const float px = x + dx * step, py = y + dy * step;
                if (CanTraverse(px, py, px, py, radius)) { x = px; y = py; return true; }
            }
    return false;
}

float ShapeCollisionMap::SpawnX() const { return m_data ? m_data->spawnX : 0.f; }
float ShapeCollisionMap::SpawnY() const { return m_data ? m_data->spawnY : 0.f; }
float ShapeCollisionMap::SpawnYaw() const { return m_data ? m_data->spawnYaw : 0.f; }
size_t ShapeCollisionMap::PolygonCount() const { return m_data ? m_data->polygons.size() : 0; }
