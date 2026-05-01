export module QueuePool;

import Common;

export class QueuePool
{
public:
	QueuePool(uint32 blockSize, uint32 count)
		: m_blockSize(blockSize)
		, isExternalMemory(false)
	{
		m_memory = new byte[blockSize * count];

		for (auto i = 0; i < count; ++i)
		{
			m_queue.push(m_memory + i * blockSize);
		}
	}

	// 이미 외부에서 관리되는 객체 접근 (data Segment)
	QueuePool(uint32 blockSize, uint32 count, byte* externalMemory)
		: m_blockSize(blockSize)
		, isExternalMemory(true)
	{
		m_memory = externalMemory;

		for (auto i = 0; i < count; ++i)
		{
			m_queue.push(m_memory + i * blockSize);
		}
	}
	
	~QueuePool()
	{
		if (!isExternalMemory)
		{
			delete[] m_memory;
		}
	}

	void* Alloc()
	{
		if (m_queue.empty())
		{
			return nullptr;
		}
		void* ptr = m_queue.front();
		m_queue.pop();
		return ptr;
	}


	void Free(void* ptr)
	{
		m_queue.push(static_cast<byte*>(ptr));
	}

private:
	bool isExternalMemory = false;
	byte* m_memory = nullptr;
	uint32 m_blockSize = 0;
	std::queue<byte*> m_queue;
};