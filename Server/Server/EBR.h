#pragma once
#include "pch.h"
#include "Define.h"

namespace EBR
{
	constexpr int STRIDE = 16;

	extern std::atomic<int> g_epoch;
	extern std::atomic<int> g_threadEpoch[MAX_WORKER_THREADS * STRIDE];
	extern std::atomic<int> g_nextTid;
	extern thread_local int tl_tid;

	void RegisterThread();
	void UnregisterThread();
	void Start();
	void End();
	bool IsSafe(int retireEpoch);
	int  Now();
}

template<typename T, size_t N>
class EBRPool
{
public:
	void Init()
	{
		for (uint32_t i = 0; i < N; ++i)
			m_init.push(i);
	}

	bool Acquire(uint32_t& out)
	{
		int tid = EBR::tl_tid;
		if (tid >= 0 && !m_retire[tid].empty() && EBR::IsSafe(m_retire[tid].front().epoch))
		{
			out = m_retire[tid].front().idx;
			m_retire[tid].pop();
			m_obj[out].Clear();
			return true;
		}
		return m_init.try_pop(out);
	}

	void Retire(uint32_t idx)
	{
		m_retire[EBR::tl_tid].push({ EBR::Now(), idx });
	}

	void Release(uint32_t idx)
	{
		m_init.push(idx);
	}

	T* Get(uint32_t idx) { return &m_obj[idx]; }

private:
	struct Retired { int epoch; uint32_t idx; };

	T                               m_obj[N];
	tbb::concurrent_queue<uint32_t> m_init;
	alignas(64) std::queue<Retired> m_retire[MAX_WORKER_THREADS];
};
