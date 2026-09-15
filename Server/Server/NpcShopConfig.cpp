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
    }
    lua_close(L);
    if (!valid) return false;
    products=std::move(next); enabled=true;
    Log::Success("[LUA] shop loaded: {} products (HP potion {}G, MP potion {}G, {} equipment)",
        products.size(),products[0].basePrice,products[1].basePrice,products.size()-2);
    return true;
}
