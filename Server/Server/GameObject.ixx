export module GameObject;
import Common;

export class GameObject
{
public:
	GameObject() = default;
	virtual ~GameObject() = default;

	virtual void Init(uint32 id)
	{
		m_index = id;
	}

	virtual void Clear() {}

	constexpr uint32 GetIndex() const { return m_index; }

	float x{}, y{}, z{ 88 };

protected:
	uint32 m_index = 0;
};
