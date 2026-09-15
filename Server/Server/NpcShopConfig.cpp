#include "pch.h"
#include "NpcShopConfig.h"
#include "include/lua.hpp"

bool NpcShopConfig::Load(const char* path)
{
    enabled=false;
    lua_State* L=luaL_newstate();
    if (!L) return false;
    auto next=products;
    bool valid=luaL_dofile(L,path)==LUA_OK;
    if (!valid) Log::Warning("[LUA] '{}' load fail: {}",path,lua_tostring(L,-1));
    else
    {
        const char* keys[SHOP_PRODUCT_COUNT]={"HealthPotion","ManaPotion"};
        const char* fields[]={"minPrice","avgPrice","maxPrice"};
        lua_getglobal(L,"ShopPotions");
        valid=lua_istable(L,-1);
        for (int i=0;valid && i<SHOP_PRODUCT_COUNT;++i)
        {
            lua_getfield(L,-1,keys[i]);
            int* values[]={&next[i].minimumPrice,&next[i].basePrice,&next[i].maximumPrice};
            valid=lua_istable(L,-1);
            for (int f=0;valid && f<3;++f)
            {
                lua_getfield(L,-1,fields[f]);
                int isInteger=0;
                const lua_Integer value=lua_tointegerx(L,-1,&isInteger);
                valid=isInteger && value>=1 && value<=100000;
                if (valid) *values[f]=static_cast<int>(value);
                lua_pop(L,1);
            }
            valid=valid && next[i].minimumPrice<=next[i].basePrice && next[i].basePrice<=next[i].maximumPrice;
            lua_pop(L,1);
        }
        if (!valid) Log::Warning("[LUA] invalid ShopPotions in '{}' (integers 1..100000, minPrice <= avgPrice <= maxPrice)",path);
    }
    lua_close(L);
    if (!valid) return false;
    products=next; enabled=true;
    Log::Success("[LUA] shop prices loaded: HP {}/{}/{}G, MP {}/{}/{}G",
        products[0].minimumPrice,products[0].basePrice,products[0].maximumPrice,
        products[1].minimumPrice,products[1].basePrice,products[1].maximumPrice);
    return true;
}
