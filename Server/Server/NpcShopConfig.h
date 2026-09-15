#pragma once
#include "../../Common/Protocol.h"
#include <array>

// Shop.lua 로 적재되는 포션 가격. index = item id - 1 (0 회복 포션, 1 마나 포션).
struct NpcShopConfig
{
    struct Product { int basePrice=100, minimumPrice=70, maximumPrice=120; };
    std::array<Product,SHOP_PRODUCT_COUNT> products;
    bool enabled=true;
    bool Load(const char* path); // 실패하면 상점을 끈다
};
inline NpcShopConfig g_npcShopConfig;
