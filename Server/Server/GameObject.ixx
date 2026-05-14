export module GameObject;
import Common;

export class GameObject
{
public:
	GameObject() = default;
	virtual ~GameObject() = default;

	virtual void Init(uint32 id)
	{
		m_id = id;
	}

	virtual void InitClear() = 0;

	constexpr uint32 GetId() const { return m_id; }

	float x{}, y{}, z{ 88 };
protected:
	uint32 m_id = 0;
};
