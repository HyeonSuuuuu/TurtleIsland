#pragma once
#include "../../Common/NpcMcp.h"
#include "../../Common/GameplayRules.h"
#include "NpcShopConfig.h"
#include "MonsterScript.h"
#include "Quest.h"
#include <cmath>
#include <deque>
#include <algorithm>

// No gold, inventory, account identifier or unrelated NPC state is serialized here.
struct NpcConversation
{
    struct Relationship
    {
        uint64_t lastAffinityAt = 0, windowAt = 0;
        int affinity = g_npcShopConfig.affinity.start, windowGain = 0;
        std::deque<std::string> rewardedMessages;
    } relationship, proposedRelationship;
    std::string conversation, context, message;
    uint64_t expiresAt = 0;
    bool initialized = false, ready = false, changedAffinity = false;
    size_t calls = 0;
    NpcMcp::Json replies = NpcMcp::Json::object();
    // quote survives later turns until bought, replaced, or invalidated; previousQuote restores it on failure.
    NpcMcp::Json quote, previousQuote;
    // Quest accepts (false) and completions (true) proposed this turn; the room applies them only if the reply completes.
    std::vector<std::pair<bool, uint16_t>> questActions;

    void Begin(const std::string& token, const std::string& text, uint64_t now)
    {
        if (!context.empty()) quote = previousQuote; // an unfinished turn never commits its quote
        context = token; message = text; expiresAt = now + NpcMcp::TurnLifetimeMs;
        initialized = ready = changedAffinity = false; calls = 0; questActions.clear();
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
        const auto* product = g_npcShopConfig.Find(itemId);
        return product ? product->basePrice : 0;
    }
    void ConsumeQuote(int itemId, int quantity)
    {
        if (quote.is_null() || quote["item_id"] != itemId) return;
        const int left = quote["quantity"].get<int>() - quantity;
        if (left > 0) quote["quantity"] = left; else quote = nullptr;
        previousQuote = quote;
    }
    // The full list stays short so it fits one MCP frame; prices per item come from Offer.
    NpcMcp::Json Catalog() const
    {
        auto products=NpcMcp::Json::array();
        for (const auto& p:g_npcShopConfig.products)
            products.push_back({{"item_id",p.itemId},{"name",p.name},{"base_price",p.basePrice}});
        return products;
    }
    NpcMcp::Json Offer(const NpcShopConfig::Product& p) const
    {
        const int minimum=p.basePrice-(p.basePrice-p.minimumPrice)*proposedRelationship.affinity/100;
        NpcMcp::Json offer={{"item_id",p.itemId},{"name",p.name},{"base_price",p.basePrice},{"absolute_floor",p.minimumPrice},
            {"minimum_price",minimum},{"maximum_price",p.maximumPrice}};
        if (p.equipment)
            offer["stats"]={{"hp",p.stats.hp},{"attack",p.stats.attack},{"defense",p.stats.defense},{"move_speed",p.stats.moveSpeed}};
        else
            offer["effect"]=p.itemId==GameplayRules::HealthPotion ? "restores 50 HP" : "restores 50 MP";
        return offer;
    }
    // Rough fight estimate from server stats: time for this customer to win with basic attacks vs. time to fall.
    static const char* Danger(const MonsterDef& monster, const GameplayRules::PlayerState& player)
    {
        const auto& basic=GameplayRules::Skills[SKILL_ID_BASIC_ATTACK];
        const double toWin=std::ceil(monster.hp/static_cast<double>(std::max(1,basic.damage+player.attack-20)))*
            std::max(basic.cooldownMs,basic.movementLockMs);
        const double toFall=std::ceil(player.maxHP/static_cast<double>(std::max(1,monster.attackDamage-player.defense)))*monster.attackCooldownMs;
        const double ratio=toWin/toFall;
        return ratio<0.5 ? "easy" : ratio<1. ? "fair" : ratio<2. ? "hard" : "deadly";
    }
    // The quest log as it will be if this turn completes.
    QuestLog StagedQuests(const QuestLog& committed, GameplayRules::PlayerState player) const
    {
        QuestLog staged=committed;
        for (const auto& [complete,id]:questActions)
            if (const auto* quest=FindQuest(id)) complete ? staged.Complete(*quest,player) : staged.Accept(*quest,player.level);
        return staged;
    }
    NpcMcp::Json Call(const std::string& name, const NpcMcp::Json& args, const GameplayRules::PlayerState& player, uint64_t now,
        const QuestLog* quests = nullptr)
    {
        using namespace NpcMcp;
        auto& [lastAffinityAt, windowAt, affinity, windowGain, rewardedMessages] = proposedRelationship;
        if (!args.is_object()) return ToolError("invalid_arguments");
        if (name == "get_player_condition" && args.empty())
            return ToolResult({{"hp",player.hp},{"max_hp",player.maxHP},{"level",player.level}});
        if (name == "get_shop_catalog" && args.empty()) return ToolResult({{"products",Catalog()}});
        if (name == "get_shop_catalog")
        {
            const auto* product = args.size()==1 && Integer(args,"item_id",1,65535) ? g_npcShopConfig.Find(args["item_id"].get<int>()) : nullptr;
            return product ? ToolResult(Offer(*product)) : ToolError("unknown_item");
        }
        if (name == "get_monster_info" && args.empty())
        {
            auto monsters=Json::array();
            for (size_t i=0;i<g_monsterDefs.size();++i) monsters.push_back({{"monster_id",i},{"name",g_monsterDefs[i].name}});
            return ToolResult({{"monsters",monsters}});
        }
        if (name == "get_monster_info")
        {
            if (args.size()!=1 || !Integer(args,"monster_id",0,255) || args["monster_id"].get<size_t>()>=g_monsterDefs.size())
                return ToolError("unknown_monster");
            const auto& monster=g_monsterDefs[args["monster_id"].get<size_t>()];
            Json info={{"name",monster.name},{"danger_for_customer",Danger(monster,player)},
                {"hunter_reward_gold",monster.gold},{"hunter_reward_exp",monster.exp}};
            for (const auto& [key,text]:{std::pair{"habitat",&monster.habitat},{"looks",&monster.looks},{"lore",&monster.lore}})
                if (!text->empty()) info[key]=*text;
            return ToolResult(info);
        }
        if (name == "get_quests" || name == "accept_quest" || name == "complete_quest")
        {
            if (!quests) return ToolError("quests_unavailable");
            const QuestLog staged=StagedQuests(*quests,player);
            if (name == "get_quests" && args.empty())
            {
                auto list=Json::array();
                for (const auto& quest:g_questDefs)
                    if (const auto status=staged.Status(quest,player.level); status!=QuestStatus::Locked && status!=QuestStatus::Done)
                    {
                        // The chief offers quests straight from this list, so it has to carry the numbers
                        // he quotes. Without them he made up goals and rewards.
                        Json entry={{"quest_id",quest.id},{"title",quest.title},{"status",QuestStatusName(status)},
                            {"goal",quest.count},{"reward_gold",quest.gold},{"reward_exp",quest.exp}};
                        if (g_monsterDefs.size()>quest.monsterType) entry["target"]=g_monsterDefs[quest.monsterType].name;
                        if (const auto kills=staged.active.find(quest.id); kills!=staged.active.end()) entry["progress"]=std::to_string(kills->second)+"/"+std::to_string(quest.count);
                        list.push_back(entry);
                    }
                return ToolResult({{"quests",list},{"completed_count",staged.done.size()},{"max_active",QUEST_MAX_ACTIVE}});
            }
            const auto* quest=args.size()==1 && Integer(args,"quest_id",1,65535) ? FindQuest(args["quest_id"].get<uint16_t>()) : nullptr;
            if (!quest) return ToolError("unknown_quest");
            const auto status=staged.Status(*quest,player.level);
            if (name == "get_quests")
            {
                const auto kills=staged.active.find(quest->id);
                return ToolResult({{"quest_id",quest->id},{"title",quest->title},{"description",quest->description},{"status",QuestStatusName(status)},
                    {"target",g_monsterDefs.size()>quest->monsterType ? g_monsterDefs[quest->monsterType].name : ""},
                    {"progress",kills!=staged.active.end() ? kills->second : 0},{"goal",quest->count},
                    {"reward_gold",quest->gold},{"reward_exp",quest->exp},{"min_level",quest->minLevel}});
            }
            QuestLog trial=staged;
            GameplayRules::PlayerState rewarded=player;
            const bool complete=name=="complete_quest";
            if (complete ? !trial.Complete(*quest,rewarded) : !trial.Accept(*quest,player.level))
                return ToolError(complete ? "not_ready_to_report" : status==QuestStatus::Available ? "too_many_active_quests" : "not_available");
            questActions.push_back({complete,quest->id});
            return complete ? ToolResult({{"completed",true},{"title",quest->title},{"reward_gold",quest->gold},{"reward_exp",quest->exp},{"paid",false}})
                : ToolResult({{"accepted",true},{"title",quest->title},{"goal",quest->count}});
        }
        if (name == "get_relationship" && args.empty()) return ToolResult({{"affinity",affinity},{"minimum",0},{"maximum",100}});
        if (name == "propose_price")
        {
            if (args.size()!=3 || !Integer(args,"item_id",1,65535) || !Integer(args,"quantity",1,99) || !Integer(args,"unit_price",1,1000000))
                return ToolError("invalid_arguments");
            const auto* item = g_npcShopConfig.Find(args["item_id"].get<int>());
            if (!item) return ToolError("unknown_item");
            const auto product = Offer(*item);
            if (args["unit_price"].get<int>() < product["minimum_price"].get<int>()) return ToolError("below_allowed_price");
            if (args["unit_price"].get<int>() > product["maximum_price"].get<int>()) return ToolError("above_allowed_price");
            quote = args;
            return ToolResult({{"accepted",true},{"quote",quote},{"purchase_executed",false}});
        }
        if (name == "propose_affinity_change")
        {
            const auto& rules = g_npcShopConfig.affinity;
            if (args.size()!=1 || !Integer(args,"delta",-rules.maxStep,rules.maxStep)) return ToolError("invalid_arguments");
            const int delta = args["delta"].get<int>();
            if (delta == 0) return ToolResult({{"affinity",affinity},{"applied_delta",0}});
            if (changedAffinity || (lastAffinityAt && now-lastAffinityAt < static_cast<uint64_t>(rules.cooldownMs))) return ToolError("relationship_cooldown");
            if (now-windowAt >= static_cast<uint64_t>(rules.windowMs)) { windowAt=now; windowGain=0; }
            if (delta > 0 && (windowGain+delta > rules.windowGain || std::find(rewardedMessages.begin(),rewardedMessages.end(),message)!=rewardedMessages.end()))
                return ToolError("relationship_repeat_limit");
            const int before = affinity;
            affinity = std::clamp(affinity+delta,0,100);
            if (delta>0)
            {
                windowGain += delta; rewardedMessages.push_back(message);
                if (rewardedMessages.size()>32) rewardedMessages.pop_front();
            }
            lastAffinityAt=now; changedAffinity=true;
            if (!quote.is_null())
                if (const auto* item = g_npcShopConfig.Find(quote["item_id"].get<int>());
                    !item || quote["unit_price"].get<int>() < Offer(*item)["minimum_price"].get<int>()) quote=nullptr;
            return ToolResult({{"affinity",affinity},{"applied_delta",affinity-before}});
        }
        return ToolError("unknown_tool_or_arguments");
    }
    NpcMcp::Json Dispatch(const NpcMcp::Json& request, const GameplayRules::PlayerState& player, uint64_t now, const QuestLog* quests = nullptr)
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
        if (replies.size()>=32) return Error(id,-32000,"Request limit"); // initialize + tools/list pages + catalog + tool calls
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
            const auto tools=Tools(g_npcShopConfig.affinity.maxStep);
            if ((!cursor.empty() && (cursor[0]<'0' || static_cast<size_t>(cursor[0]-'0')>=tools.size())) || (params.contains("cursor") && cursor.empty()))
                return Error(id,-32602,"Invalid cursor");
            const size_t index=cursor.empty()?0:cursor[0]-'0';
            result={{"tools",Json::array({tools[index]})}};
            if (index+1<tools.size()) result["nextCursor"]=std::to_string(index+1);
        }
        else if (method=="tools/call")
        {
            if (++calls>MaxToolCalls) result=ToolError("tool_limit");
            else result=Call(Text(params,"name",64),params.value("arguments",Json::object()),player,now,quests);
        }
        else return Error(id,-32601,"Method not found");
        auto reply=Result(id,result);
        replies[key]=reply;
        return reply;
    }
};
