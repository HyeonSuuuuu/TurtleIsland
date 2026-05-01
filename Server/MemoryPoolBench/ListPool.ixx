export module ListPool;


import Common;

export class ListPool
{
	struct Node
	{
		Node* next = nullptr;
	};
public: // blockSize가 8 이상이여야함 (포인터 대체)
	ListPool(uint32 blockSize, uint32 count)
		: m_blockSize(blockSize < sizeof(Node) ? sizeof(Node) : blockSize)
		, isExternalMemory(false)
	{
		m_momory = new byte[m_blockSize * count];
		for (auto i = 0; i < count; ++i)
		{
			auto node = reinterpret_cast<Node*>(m_momory + i * m_blockSize);
			node->next = m_head;
			m_head = node;
		}
	}
	// 이미 외부에서 관리되는 객체 접근 (data Segment)
	ListPool(uint32 blockSize, uint32 count, byte* externalMemory)
		: m_blockSize(blockSize < sizeof(Node) ? sizeof(Node) : blockSize)
		, isExternalMemory(true)
	{
		m_momory = externalMemory;
		for (auto i = 0; i < count; ++i)
		{
			auto node = reinterpret_cast<Node*>(m_momory + i * m_blockSize);
			node->next = m_head;
			m_head = node;
		}
	}

	~ListPool()
	{
		if (!isExternalMemory)
		{
			delete[] m_momory;
		}
	}

	void* Alloc()
	{
		if (m_head == nullptr)
			return nullptr;
		Node* node = m_head;
		m_head = m_head->next;
		return node;
	}

	void Free(void* ptr)
	{
		Node* node = reinterpret_cast<Node*>(ptr);
		node->next = m_head;
		m_head = node;
	}
private:
	bool isExternalMemory = false;
	byte* m_momory = nullptr;
	uint32 m_blockSize = 0;
	Node* m_head = nullptr;
};