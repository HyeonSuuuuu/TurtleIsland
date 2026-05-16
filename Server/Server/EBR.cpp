#include "pch.h"
#include "EBR.h"

namespace EBR
{
	std::atomic<int> g_epoch{ 1 };
	std::atomic<int> g_threadEpoch[MAX_WORKER_THREADS * STRIDE]{};
	std::atomic<int> g_nextTid{ 0 };
	thread_local int tl_tid = -1;

	void RegisterThread()
	{
		tl_tid = g_nextTid.fetch_add(1);
	}

	void UnregisterThread()
	{
		if (tl_tid >= 0)
			g_threadEpoch[tl_tid * STRIDE] = 0;
	}

	void Start()
	{
		g_threadEpoch[tl_tid * STRIDE] = ++g_epoch;
	}

	void End()
	{
		g_threadEpoch[tl_tid * STRIDE] = 0;
	}

	bool IsSafe(int retireEpoch)
	{
		for (auto i = 0; i < MAX_WORKER_THREADS; ++i)
		{
			int e = g_threadEpoch[i * STRIDE].load();
			if (e != 0 && e < retireEpoch)
				return false;
		}
		return true;
	}

	int Now()
	{
		return g_epoch.load();
	}
}
