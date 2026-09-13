#pragma once
#include <cstddef>
#include <memory>
#include <string>

// Immutable static XY collision scene. Public positions/radii are Unreal cm.
class ShapeCollisionMap
{
public:
    bool Load(const char* path);
    bool IsLoaded() const { return !!m_data; }
    bool CanTraverse(float fromX, float fromY, float toX, float toY, float radius = 0.f) const;
    bool SnapToFree(float& x, float& y, float radius) const;
    const std::string& LastError() const { return m_error; }
    float SpawnX() const;
    float SpawnY() const;
    float SpawnGroundZ() const;
    float SpawnYaw() const;
    size_t PolygonCount() const;

private:
    struct Data;
    std::shared_ptr<const Data> m_data;
    std::string m_error;
};
