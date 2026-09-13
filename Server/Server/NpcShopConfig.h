#pragma once
#include "../../Common/NpcMcp.h"
#include <array>
#include <fstream>

struct NpcShopConfig
{
    struct Product { int basePrice=100, minimumPrice=70, maximumPrice=120; };
    std::array<Product,2> products;
    bool enabled=true;
    bool Load(const char* path)
    {
        using namespace NpcMcp;
        try
        {
            std::ifstream file(path);
            if (!file) { enabled=false; return false; }
            Json data; file>>data;
            if (!data.is_object() || !data.contains("products") || !data["products"].is_array() || data["products"].size()!=2)
                throw std::runtime_error("Invalid shop config");
            std::array<Product,2> next;
            for (int i=0;i<2;++i)
            {
                const auto& p=data["products"][i];
                if (!p.is_object() || !Integer(p,"item_id",i+1,i+1) || !Integer(p,"base_price",1,100000) ||
                    !Integer(p,"minimum_price",1,100000) || !Integer(p,"maximum_price",1,100000)) throw std::runtime_error("Invalid product");
                next[i]={p["base_price"],p["minimum_price"],p["maximum_price"]};
                if (next[i].minimumPrice>next[i].basePrice || next[i].basePrice>next[i].maximumPrice) throw std::runtime_error("Invalid prices");
            }
            products=next; enabled=true; return true;
        }
        catch (const std::exception&) { enabled=false; return false; }
    }
};
inline NpcShopConfig g_npcShopConfig;
