#pragma once
#include "../../Common/Protocol.h"
#include "../../Common/GameplayRules.h"
#include <string>
#include <vector>

// Shop.lua 로 적재되는 상품. 0·1번은 회복/마나 포션, 그 뒤는 장비.
struct NpcShopConfig
{
    struct Product
    {
        uint16_t itemId=0;
        std::string name;
        int basePrice=100, minimumPrice=70, maximumPrice=120;
        bool equipment=false;
        GameplayRules::EquipmentStats stats;
    };
    std::vector<Product> products{
        {GameplayRules::HealthPotion,"회복 포션"},
        {GameplayRules::ManaPotion,"마나 포션"}};
    // Shop.lua ShopAffinity. Defaults are the strict rules; the booth config relaxes them.
    struct AffinityRules { int start=25, maxStep=2, cooldownMs=60000, windowGain=10, windowMs=300000; } affinity;
    bool enabled=true;
    bool Load(const char* path); // 실패하면 상점을 끈다
    const Product* Find(int itemId) const
    {
        for (const auto& p:products) if (p.itemId==itemId) return &p;
        return nullptr;
    }
};
inline NpcShopConfig g_npcShopConfig;
