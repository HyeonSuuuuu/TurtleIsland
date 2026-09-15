#pragma once
#include "../../Common/NpcMcp.h"
#include "../../Common/GameplayRules.h"
#include "NpcShopConfig.h"
#include <deque>
#include <algorithm>

// No gold, inventory, account identifier or unrelated NPC state is serialized here.
struct NpcConversation
{
    struct Relationship
    {
        uint64_t lastAffinityAt = 0, windowAt = 0;
        int affinity = 25, windowGain = 0;
        std::deque<std::string> rewardedMessages;
    } relationship, proposedRelationship;
    std::string conversation, context, message;
    uint64_t expiresAt = 0;
    bool initialized = false, ready = false, changedAffinity = false;
    size_t calls = 0;
    NpcMcp::Json replies = NpcMcp::Json::object();
    // quote survives later turns until bought, replaced, or invalidated; previousQuote restores it on failure.
    NpcMcp::Json quote, previousQuote;

    void Begin(const std::string& token, const std::string& text, uint64_t now)
    {
        if (!context.empty()) quote = previousQuote; // an unfinished turn never commits its quote
        context = token; message = text; expiresAt = now + NpcMcp::TurnLifetimeMs;
        initialized = ready = changedAffinity = false; calls = 0;
        proposedRelationship = relationship;
        replies = NpcMcp::Json::object(); previousQuote = quote;
    }
    // True while a turn may still commit. An expired turn's staged quote is dropped here.
    bool Busy(uint64_t now)
    {
        if (context.empty()) return false;
        if (now < expiresAt) return true;
        context.clear(); quote = previousQuote;
        return false;
    }
    const NpcMcp::Json& CommittedQuote() const { return context.empty() ? quote : previousQuote; }
    // Quote price applies to up to its remaining quantity; otherwise the base price.
    int UnitPrice(int itemId, int quantity) const
    {
        const auto& q = CommittedQuote();
        if (!q.is_null() && q["item_id"] == itemId && quantity <= q["quantity"].get<int>())
            return q["unit_price"].get<int>();
        return g_npcShopConfig.products[itemId-1].basePrice;
    }
    void ConsumeQuote(int itemId, int quantity)
    {
        if (quote.is_null() || quote["item_id"] != itemId) return;
        const int left = quote["quantity"].get<int>() - quantity;
        if (left > 0) quote["quantity"] = left; else quote = nullptr;
        previousQuote = quote;
    }
    NpcMcp::Json Catalog() const
    {
        auto products=NpcMcp::Json::array();
        for (int i=0;i<2;++i)
        {
            const auto& p=g_npcShopConfig.products[i];
            const int minimum=p.basePrice-(p.basePrice-p.minimumPrice)*proposedRelationship.affinity/100;
            products.push_back({{"item_id",i+1},{"name",i==0?"HP potion":"MP potion"},{"base_price",p.basePrice},
                {"absolute_floor",p.minimumPrice},{"minimum_price",minimum},{"maximum_price",p.maximumPrice}});
        }
        return products;
    }
    NpcMcp::Json Call(const std::string& name, const NpcMcp::Json& args, const GameplayRules::PlayerState& player, uint64_t now)
    {
        using namespace NpcMcp;
        auto& [lastAffinityAt, windowAt, affinity, windowGain, rewardedMessages] = proposedRelationship;
        if (!args.is_object()) return ToolError("invalid_arguments");
        if (name == "get_player_condition" && args.empty())
            return ToolResult({{"hp",player.hp},{"max_hp",player.maxHP},{"level",player.level}});
        if (name == "get_shop_catalog" && args.empty()) return ToolResult({{"products",Catalog()}});
        if (name == "get_relationship" && args.empty()) return ToolResult({{"affinity",affinity},{"minimum",0},{"maximum",100}});
        if (name == "propose_price")
        {
            if (args.size()!=3 || !Integer(args,"item_id",1,2) || !Integer(args,"quantity",1,99) || !Integer(args,"unit_price",1,100000))
                return ToolError("invalid_arguments");
            const auto product = Catalog().at(args["item_id"].get<int>()-1);
            if (args["unit_price"].get<int>() < product["minimum_price"].get<int>()) return ToolError("below_allowed_price");
            if (args["unit_price"].get<int>() > product["maximum_price"].get<int>()) return ToolError("above_allowed_price");
            quote = args;
            return ToolResult({{"accepted",true},{"quote",quote},{"purchase_executed",false}});
        }
        if (name == "propose_affinity_change")
        {
            if (args.size()!=1 || !Integer(args,"delta",-2,2)) return ToolError("invalid_arguments");
            const int delta = args["delta"].get<int>();
            if (delta == 0) return ToolResult({{"affinity",affinity},{"applied_delta",0}});
            if (changedAffinity || (lastAffinityAt && now-lastAffinityAt < 60000)) return ToolError("relationship_cooldown");
            if (now-windowAt >= 300000) { windowAt=now; windowGain=0; }
            if (delta > 0 && (windowGain+delta > 10 || std::find(rewardedMessages.begin(),rewardedMessages.end(),message)!=rewardedMessages.end()))
                return ToolError("relationship_repeat_limit");
            const int before = affinity;
            affinity = std::clamp(affinity+delta,0,100);
            if (delta>0)
            {
                windowGain += delta; rewardedMessages.push_back(message);
                if (rewardedMessages.size()>32) rewardedMessages.pop_front();
            }
            lastAffinityAt=now; changedAffinity=true;
            if (!quote.is_null() && quote["unit_price"].get<int>() < Catalog().at(quote["item_id"].get<int>()-1)["minimum_price"].get<int>())
                quote=nullptr;
            return ToolResult({{"affinity",affinity},{"applied_delta",affinity-before}});
        }
        return ToolError("unknown_tool_or_arguments");
    }
    NpcMcp::Json Dispatch(const NpcMcp::Json& request, const GameplayRules::PlayerState& player, uint64_t now)
    {
        using namespace NpcMcp;
        if (!request.is_object()) return Error(nullptr,-32600,"Invalid request");
        const auto id = request.value("id", Json{});
        if (Text(request,"jsonrpc",3)!="2.0" ||
            (!id.is_null() && !id.is_number_integer() && !id.is_string())) return Error(nullptr,-32600,"Invalid request");
        if (context.empty() || now >= expiresAt || !player.ready || player.hp <= 0) return Error(id,-32001,"Expired NPC context");
        const auto method = Text(request,"method",64);
        const auto params = request.value("params",Json::object());
        if (!params.is_object()) return Error(id,-32602,"Invalid params");
        if (method == "notifications/initialized" && id.is_null()) { ready=initialized; return nullptr; }
        if (id.is_null()) return nullptr;
        const std::string key = id.dump();
        if (replies.contains(key)) return replies[key];
        if (replies.size()>=20) return Error(id,-32000,"Request limit");
        Json result;
        if (method=="initialize")
        {
            initialized=true;
            result={{"protocolVersion",Version},{"capabilities",{{"tools",Json::object()}}},{"serverInfo",{{"name","TurtleIsland NPC tools"},{"version","1.0.0"}}}};
        }
        else if (!ready) return Error(id,-32000,"Initialize first");
        else if (method=="ping") result=Json::object();
        else if (method=="tools/list")
        {
            const auto cursor=Text(params,"cursor",1);
            if ((!cursor.empty() && (cursor[0]<'0' || cursor[0]>'4')) || (params.contains("cursor") && cursor.empty()))
                return Error(id,-32602,"Invalid cursor");
            const size_t index=cursor.empty()?0:cursor[0]-'0';
            result={{"tools",Json::array({Tools()[index]})}};
            if (index+1<Tools().size()) result["nextCursor"]=std::to_string(index+1);
        }
        else if (method=="tools/call")
        {
            if (++calls>MaxToolCalls) result=ToolError("tool_limit");
            else result=Call(Text(params,"name",64),params.value("arguments",Json::object()),player,now);
        }
        else return Error(id,-32601,"Method not found");
        auto reply=Result(id,result);
        replies[key]=reply;
        return reply;
    }
};
