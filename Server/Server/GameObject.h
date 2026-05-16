#pragma once
#include "pch.h"

class GameObject
{
public:
	GameObject() = default;
	virtual ~GameObject() = default;

	virtual void Init(uint32_t id)
	{
		m_id = id;
	}

	virtual void Clear() = 0;

	constexpr uint32_t GetId() const { return m_id; }

	float x{}, y{}, z{ 88 };
protected:
	uint32_t m_id = 0;
};
