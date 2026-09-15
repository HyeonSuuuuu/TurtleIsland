#pragma once
#include "../../Common/Protocol.h"
#include "../../Common/GameplayRules.h"
#include <map>
#include <set>
#include <string>
#include <vector>

// 촌장 퀘스트 (Quest.lua). 지금은 몬스터 처치형만 있다.
struct QuestDef
{
	uint16_t id = 0;
	std::string title, description;
	uint16_t monsterType = 0; // zero-based Monsters index
	uint16_t count = 1;
	int gold = 0, exp = 0;
	int minLevel = 1;
	uint16_t prerequisite = 0; // quest that must be completed first, 0 = none
};

inline std::vector<QuestDef> g_questDefs;

inline const QuestDef* FindQuest(uint16_t id)
{
	for (const auto& quest : g_questDefs) if (quest.id == id) return &quest;
	return nullptr;
}

enum class QuestStatus : uint8_t { Locked, Available, Active, Ready, Done };

inline const char* QuestStatusName(QuestStatus status)
{
	static const char* names[] = {"locked", "available", "active", "ready_to_report", "done"};
	return names[static_cast<size_t>(status)];
}

// One player's quests. Kept by the room and saved with PlayerProgress.
struct QuestLog
{
	std::map<uint16_t, uint16_t> active; // quest id -> kills so far
	std::set<uint16_t> done;

	QuestStatus Status(const QuestDef& quest, int level) const
	{
		if (done.contains(quest.id)) return QuestStatus::Done;
		if (const auto found = active.find(quest.id); found != active.end())
			return found->second >= quest.count ? QuestStatus::Ready : QuestStatus::Active;
		return level >= quest.minLevel && (!quest.prerequisite || done.contains(quest.prerequisite)) ? QuestStatus::Available : QuestStatus::Locked;
	}
	bool Accept(const QuestDef& quest, int level)
	{
		if (Status(quest, level) != QuestStatus::Available || active.size() >= static_cast<size_t>(QUEST_MAX_ACTIVE)) return false;
		active[quest.id] = 0;
		return true;
	}
	// The reward goes through RewardKill so level-ups follow the same rules as hunting.
	bool Complete(const QuestDef& quest, GameplayRules::PlayerState& player)
	{
		if (Status(quest, player.level) != QuestStatus::Ready) return false;
		active.erase(quest.id);
		done.insert(quest.id);
		GameplayRules::RewardKill(player, quest.gold, quest.exp);
		return true;
	}
	bool OnKill(uint16_t monsterType)
	{
		bool changed = false;
		for (auto& [id, kills] : active)
			if (const auto* quest = FindQuest(id); quest && quest->monsterType == monsterType && kills < quest->count) { ++kills; changed = true; }
		return changed;
	}
};
