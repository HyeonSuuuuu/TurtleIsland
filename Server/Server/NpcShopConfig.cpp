#include "pch.h"
#include "NpcShopConfig.h"
#include "include/lua.hpp"

namespace
{
    bool ReadInt(lua_State* L, const char* field, int& out, int minimum, int maximum, bool required)
    {
        lua_getfield(L,-1,field);
        const bool missing=lua_isnil(L,-1);
        int isInteger=0;
        const lua_Integer value=lua_tointegerx(L,-1,&isInteger);
        lua_pop(L,1);
        if (missing) return !required;
        if (!isInteger || value<minimum || value>maximum) return false;
        out=static_cast<int>(value);
        return true;
    }
    bool ReadPrices(lua_State* L, NpcShopConfig::Product& p)
    {
        return ReadInt(L,"minPrice",p.minimumPrice,1,1000000,true) && ReadInt(L,"avgPrice",p.basePrice,1,1000000,true) &&
            ReadInt(L,"maxPrice",p.maximumPrice,1,1000000,true) && p.minimumPrice<=p.basePrice && p.basePrice<=p.maximumPrice;
    }
}

bool NpcShopConfig::Load(const char* path)
{
    enabled=false;
    lua_State* L=luaL_newstate();
    if (!L) return false;
    auto next=NpcShopConfig{}.products;
    auto nextAffinity=AffinityRules{};
    bool valid=luaL_dofile(L,path)==LUA_OK;
    if (!valid) Log::Warning("[LUA] '{}' load fail: {}",path,lua_tostring(L,-1));
    else
    {
        const char* keys[]={"HealthPotion","ManaPotion"};
        lua_getglobal(L,"ShopPotions");
        valid=lua_istable(L,-1);
        for (int i=0;valid && i<2;++i)
        {
            lua_getfield(L,-1,keys[i]);
            valid=lua_istable(L,-1) && ReadPrices(L,next[i]);
            lua_pop(L,1);
        }
        lua_pop(L,1);

        // Optional: equipment is held in the 6 inventory slots and adds its stats.
        lua_getglobal(L,"ShopEquipment");
        if (valid && lua_istable(L,-1))
        {
            const auto count=lua_rawlen(L,-1);
            for (size_t i=1;valid && i<=count;++i)
            {
                lua_rawgeti(L,-1,static_cast<lua_Integer>(i));
                Product p; p.equipment=true;
                int id=0;
                valid=lua_istable(L,-1) && ReadInt(L,"id",id,3,65535,true) && ReadPrices(L,p) &&
                    ReadInt(L,"hp",p.stats.hp,-100000,100000,false) && ReadInt(L,"attack",p.stats.attack,-100000,100000,false) &&
                    ReadInt(L,"defense",p.stats.defense,-100000,100000,false) && ReadInt(L,"moveSpeed",p.stats.moveSpeed,-10000,10000,false);
                if (valid)
                {
                    lua_getfield(L,-1,"name");
                    if (lua_type(L,-1)==LUA_TSTRING) p.name=lua_tostring(L,-1);
                    lua_pop(L,1);
                    p.itemId=static_cast<uint16_t>(id);
                    valid=!p.name.empty() && p.name.size()<SHOP_ITEM_NAME_BYTES &&
                        std::none_of(next.begin(),next.end(),[&](const Product& o){return o.itemId==p.itemId;}) &&
                        next.size()<SHOP_MAX_PRODUCTS;
                    if (valid) next.push_back(p);
                }
                lua_pop(L,1);
            }
        }
        lua_pop(L,1);
        if (!valid) Log::Warning("[LUA] invalid ShopPotions/ShopEquipment in '{}' (unique id 3..65535, name < {} bytes, integer prices minPrice <= avgPrice <= maxPrice, at most {} products)",
            path,SHOP_ITEM_NAME_BYTES,SHOP_MAX_PRODUCTS);

        // Optional: how easily talking raises affinity.
        lua_getglobal(L,"ShopAffinity");
        if (valid && lua_istable(L,-1))
        {
            int cooldownSeconds=nextAffinity.cooldownMs/1000, windowSeconds=nextAffinity.windowMs/1000;
            valid=ReadInt(L,"start",nextAffinity.start,0,100,false) && ReadInt(L,"maxStep",nextAffinity.maxStep,1,100,false) &&
                ReadInt(L,"cooldownSeconds",cooldownSeconds,0,3600,false) && ReadInt(L,"maxGainPerWindow",nextAffinity.windowGain,1,100,false) &&
                ReadInt(L,"windowSeconds",windowSeconds,1,3600,false);
            nextAffinity.cooldownMs=cooldownSeconds*1000; nextAffinity.windowMs=windowSeconds*1000;
            if (!valid) Log::Warning("[LUA] invalid ShopAffinity in '{}' (start 0..100, maxStep 1..100, cooldownSeconds 0..3600, maxGainPerWindow 1..100, windowSeconds 1..3600)",path);
        }
        lua_pop(L,1);
    }
    lua_close(L);
    if (!valid) return false;
    products=std::move(next); affinity=nextAffinity; enabled=true;
    Log::Info("[LUA] shop affinity: start {}, step up to {}, cooldown {}s, +{} per {}s",
        affinity.start,affinity.maxStep,affinity.cooldownMs/1000,affinity.windowGain,affinity.windowMs/1000);
    Log::Success("[LUA] shop loaded: {} products (HP potion {}G, MP potion {}G, {} equipment)",
        products.size(),products[0].basePrice,products[1].basePrice,products.size()-2);
    return true;
}
