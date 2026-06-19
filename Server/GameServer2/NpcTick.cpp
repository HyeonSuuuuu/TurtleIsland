#include "pch.h"
#include "NpcTick.h"
#include "Globals.h"   // g_rooms

namespace
{
	constexpr int TICK_INTERVAL_MS = 150;

	std::atomic_bool g_npcTickRun{ false };
	std::thread      g_npcTickThread;
}

void StartNpcTick()
{
	if (g_npcTickRun.exchange(true)) return;

	g_npcTickThread = std::thread([]
	{
		while (g_npcTickRun.load())
		{
			for (auto& room : g_rooms)
			{
				if (room.IsActive() && room.IsInGame())
					room.TickNpcs();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(TICK_INTERVAL_MS));
		}
	});

	Log::Info("[NpcTick] started ({}ms)", TICK_INTERVAL_MS);
}

void StopNpcTick()
{
	g_npcTickRun = false;
	if (g_npcTickThread.joinable())
		g_npcTickThread.join();
}
