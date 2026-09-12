#pragma once
#include "Protocol.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace GameplayRules
{
	struct SkillDef { int32_t damage, mana; uint32_t cooldownMs; float range, facingDot; };
	inline constexpr std::array<SkillDef, PLAYER_SKILL_COUNT> Skills = {{
		{20, 0, 500, 180.f, 0.2f}, {40, 20, 4000, 260.f, -1.f}, {60, 30, 6000, 350.f, 0.2f}
	}};
	inline constexpr uint16_t HealthPotion = 1, ManaPotion = 2;
	inline constexpr uint32_t ItemCooldownMs = 1000;
	inline constexpr uint32_t LandingRecoveryMs = 500;

	struct PlayerState
	{
		int32_t hp = 100, maxHP = 100, mp = 100, maxMP = 100;
		int32_t level = 1, exp = 0, expToNext = 100, gold = 0, attack = 20, defense = 3;
		float moveSpeed = 500.f;
		std::array<ItemSlot, INVENTORY_SLOT_COUNT> items = {{{HealthPotion, 3}, {ManaPotion, 3}}};
		uint32_t inventoryRevision = 1;
		std::array<uint64_t, PLAYER_SKILL_COUNT> skillReadyAt{};
		uint64_t itemReadyAt = 0;
		bool ready = false;
		float z = 96.f, velocityZ = 0.f;
		bool airborne = false;
		uint64_t landingReadyAt = 0;
	};

	inline bool ValidPose(float x, float y, float z, float yaw, float velocityZ)
	{
		return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
			std::isfinite(yaw) && std::isfinite(velocityZ) && std::abs(x) < 10000000.f &&
			std::abs(y) < 10000000.f && std::abs(z) < 10000000.f && std::abs(velocityZ) < 100000.f;
	}
	inline ActionResult UseSkill(PlayerState& state, uint16_t id, uint64_t now)
	{
		if (!state.ready || id >= Skills.size()) return ActionResult::Invalid;
		if (state.hp <= 0) return ActionResult::Dead;
		if (now < state.skillReadyAt[id]) return ActionResult::Cooldown;
		if (state.mp < Skills[id].mana) return ActionResult::NotEnoughMP;
		state.mp -= Skills[id].mana;
		state.skillReadyAt[id] = now + Skills[id].cooldownMs;
		return ActionResult::Ok;
	}
	inline ActionResult UseItem(PlayerState& state, const CS_UseItem& request, uint64_t now)
	{
		if (!state.ready || request.slot >= state.items.size()) return ActionResult::Invalid;
		if (state.hp <= 0) return ActionResult::Dead;
		auto& slot = state.items[request.slot];
		if (request.inventoryRevision != state.inventoryRevision || request.expectedItemId != slot.itemId)
			return ActionResult::StaleInventory;
		if (!slot.count || (slot.itemId != HealthPotion && slot.itemId != ManaPotion)) return ActionResult::Invalid;
		if (now < state.itemReadyAt) return ActionResult::Cooldown;
		int32_t& value = slot.itemId == HealthPotion ? state.hp : state.mp;
		const int32_t maximum = slot.itemId == HealthPotion ? state.maxHP : state.maxMP;
		if (value >= maximum) return ActionResult::NoEffect;
		value = std::min(maximum, value + 50);
		if (--slot.count == 0) slot.itemId = 0;
		++state.inventoryRevision;
		state.itemReadyAt = now + ItemCooldownMs;
		return ActionResult::Ok;
	}
	inline void RewardKill(PlayerState& state)
	{
		state.gold = std::min(state.gold, std::numeric_limits<int32_t>::max() - 10) + 10;
		if (state.level >= 30) return;
		state.exp += 25;
		while (state.exp >= state.expToNext && state.level < 30)
		{
			state.exp -= state.expToNext;
			++state.level;
			state.expToNext = 100 + (state.level - 1) * 50;
			state.maxHP += 10; state.hp = std::min(state.maxHP, state.hp + 10);
			state.maxMP += 5; state.mp = std::min(state.maxMP, state.mp + 5);
			state.attack += 2;
		}
		if (state.level == 30) state.exp = state.expToNext;
	}
}
