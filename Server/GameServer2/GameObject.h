#pragma once
#include "pch.h"

// 풀/네트워크 객체 공통 베이스. 콘텐츠(User/NPC)가 파생.
//   retired_epoch : 경량 EBR 회수용 stamp (Pool 이 요구)
class GameObject
{
public:
	GameObject() = default;
	virtual ~GameObject() = default;

	virtual void Init(uint32_t id) { m_id = id; }
	virtual void Clear() = 0;

	constexpr uint32_t GetId() const { return m_id; }

	unsigned long long retired_epoch = 0;

	// 2D 좌표 (z=높이는 언리얼 클라가 처리) — User/NPC 공유
	float x{}, y{}, yaw{};

protected:
	uint32_t m_id = 0;
};
