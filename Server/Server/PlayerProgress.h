#pragma once
#include "../../Common/NpcMcp.h"
#include "../../Common/GameplayRules.h"
#include "NpcShopConfig.h"
#include "Quest.h"
#include <algorithm>
#include <climits>

// What survives leaving a game, stored as JSON in dbo.CharacterProgress.
// HP/MP/attack/defense/speed are not stored: they are rebuilt from level and held equipment, so balance edits apply.
namespace PlayerProgress
{
using Json = NpcMcp::Json;
inline constexpr int Version = 1;
inline constexpr int MaxLevel = 30;

inline Json Capture(const GameplayRules::PlayerState& state, int shopAffinity, const QuestLog& quests)
{
    Json items = Json::array(), active = Json::array();
    for (const auto& slot : state.items) items.push_back({slot.itemId, slot.count});
    for (const auto& [id, kills] : quests.active) active.push_back({id, kills});
    return {{"version", Version}, {"level", state.level}, {"exp", state.exp}, {"gold", state.gold},
        {"items", items}, {"shop_affinity", shopAffinity}, {"quests", {{"active", active}, {"done", quests.done}}}};
}

// Replaces state with a new character rebuilt from saved progress. Malformed parts keep new-character defaults.
inline void Apply(GameplayRules::PlayerState& state, int& shopAffinity, QuestLog& quests, const Json& saved)
{
    GameplayRules::PlayerState rebuilt;
    quests = {};
    if (saved.is_object() && saved.value("version", 0) == Version)
    {
        const auto number = [&](const char* key, int low, int high, int fallback)
        {
            const auto found = saved.find(key);
            return found != saved.end() && found->is_number_integer() ? std::clamp(found->get<int>(), low, high) : fallback;
        };
        for (int level = number("level", 1, MaxLevel, 1); rebuilt.level < level; ++rebuilt.level)
        {
            rebuilt.maxHP += 10; rebuilt.maxMP += 5; rebuilt.attack += 2; // GameplayRules::RewardKill level-up
        }
        rebuilt.expToNext = 100 + (rebuilt.level - 1) * 50;
        rebuilt.exp = rebuilt.level == MaxLevel ? rebuilt.expToNext : number("exp", 0, rebuilt.expToNext - 1, 0);
        rebuilt.gold = number("gold", 0, INT_MAX, 0);
        shopAffinity = number("shop_affinity", 0, 100, shopAffinity);

        const auto items = saved.find("items");
        for (size_t i = 0; items != saved.end() && items->is_array() && i < std::min(items->size(), rebuilt.items.size()); ++i)
        {
            const auto& entry = (*items)[i];
            if (!entry.is_array() || entry.size() != 2 || !entry[0].is_number_integer() || !entry[1].is_number_integer()) continue;
            const int id = entry[0].get<int>(), count = entry[1].get<int>();
            const auto* product = g_npcShopConfig.Find(id);
            const bool potion = id == GameplayRules::HealthPotion || id == GameplayRules::ManaPotion;
            if (!(potion && count >= 1 && count <= GameplayRules::MaxItemStack) && !(product && product->equipment && count == 1))
            {
                rebuilt.items[i] = {};
                continue;
            }
            rebuilt.items[i] = {static_cast<uint16_t>(id), static_cast<uint16_t>(count)};
            if (product && product->equipment) // same stat rules as GameplayRules::BuyItem
            {
                rebuilt.maxHP = std::max(1, rebuilt.maxHP + product->stats.hp);
                rebuilt.attack += product->stats.attack;
                rebuilt.defense += product->stats.defense;
                rebuilt.moveSpeed = std::max(GameplayRules::MinMoveSpeed, rebuilt.moveSpeed + product->stats.moveSpeed);
            }
        }
    }
    // Quests removed from Quest.lua are dropped; kills are capped at the current goal.
    if (const auto log = saved.is_object() ? saved.find("quests") : saved.end(); saved.is_object() && log != saved.end() && log->is_object())
    {
        for (const auto& id : log->value("done", Json::array()))
            if (id.is_number_integer() && FindQuest(id.get<uint16_t>())) quests.done.insert(id.get<uint16_t>());
        for (const auto& entry : log->value("active", Json::array()))
        {
            if (quests.active.size() >= static_cast<size_t>(QUEST_MAX_ACTIVE) || !entry.is_array() || entry.size() != 2 ||
                !entry[0].is_number_integer() || !entry[1].is_number_integer()) continue;
            if (const auto* quest = FindQuest(entry[0].get<uint16_t>()); quest && !quests.done.contains(quest->id))
                quests.active[quest->id] = static_cast<uint16_t>(std::clamp(entry[1].get<int>(), 0, static_cast<int>(quest->count)));
        }
    }
    rebuilt.hp = rebuilt.maxHP; rebuilt.mp = rebuilt.maxMP;
    state = rebuilt;
}
}
