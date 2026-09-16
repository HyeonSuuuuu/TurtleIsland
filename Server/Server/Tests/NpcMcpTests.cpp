#include "pch.h"
#include "Room.h"
#include "Globals.h"
#include "PlayerProgress.h"
#include "../../../AI/Server/Server/NpcAgent.h"
#include <stdexcept>
#include <filesystem>
#include <fstream>

static void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct NpcMcpTests
{
    static bool SameItems(const GameplayRules::PlayerState& a, const GameplayRules::PlayerState& b)
    {
        return std::equal(a.items.begin(),a.items.end(),b.items.begin(),[](const auto& x,const auto& y){ return x.itemId==y.itemId && x.count==y.count; });
    }
    static void TestProgress()
    {
        using namespace GameplayRules;
        using NpcMcp::Json;
        Check(g_npcShopConfig.Load("Shop.lua") && g_npcShopConfig.Find(1004) && g_npcShopConfig.Find(1004)->equipment,"Shop.lua equipment for progress");
        PlayerState played; played.ready=true; played.gold=5000;
        Check(BuyItem(played,1004,1,2500,&g_npcShopConfig.Find(1004)->stats)==ActionResult::Ok,"equipment bought");
        RewardKill(played,300,260);
        played.hp=5; played.items[0].count=1;
        g_questDefs={{1,"first","",0,5,10,10,1,0},{2,"second","",0,1,10,10,1,1},{3,"third","",0,2,10,10,1,2}};
        QuestLog quests; quests.done.insert(1); quests.active[2]=1; quests.active[3]=1;
        const auto saved=Json::parse(PlayerProgress::Capture(played,12,quests).dump()); // through text, like the DB
        PlayerState loaded; int affinity=g_npcShopConfig.affinity.start; QuestLog loadedQuests;
        PlayerProgress::Apply(loaded,affinity,loadedQuests,saved);
        Check(loadedQuests.done==quests.done && loadedQuests.active==quests.active,"quest progress survives a save");
        Check(loaded.level==3 && loaded.exp==played.exp && loaded.expToNext==played.expToNext && loaded.gold==played.gold &&
            SameItems(loaded,played) && affinity==12,"level, exp, gold, items and affinity survive a save");
        Check(loaded.maxHP==played.maxHP && loaded.maxMP==played.maxMP && loaded.attack==played.attack && loaded.defense==played.defense &&
            loaded.moveSpeed==played.moveSpeed && loaded.hp==loaded.maxHP && loaded.mp==loaded.maxMP,"stats rebuilt from level and equipment, full HP/MP");

        auto tampered=saved;
        tampered["level"]=999; tampered["gold"]=-5; tampered["shop_affinity"]=500;
        tampered["items"]=Json::array({Json::array({1004,5}),Json::array({9999,1}),Json::array({1,3}),"junk"});
        tampered["quests"]={{"done",Json::array({1,77})},{"active",Json::array({Json::array({2,99}),Json::array({77,1}),Json::array({1,1})})}};
        PlayerState clamped; int clampedAffinity=30; QuestLog clampedQuests;
        PlayerProgress::Apply(clamped,clampedAffinity,clampedQuests,tampered);
        Check(clampedQuests.done==std::set<uint16_t>{1} && clampedQuests.active.size()==1 && clampedQuests.active.at(2)==1,
            "unknown quests are dropped, kills capped, done quests not active");
        Check(clamped.level==PlayerProgress::MaxLevel && clamped.exp==clamped.expToNext && clamped.gold==0 && clampedAffinity==100 &&
            clamped.items[0].itemId==0 && clamped.items[1].itemId==0 && clamped.items[2].itemId==1 && clamped.items[2].count==3 &&
            clamped.attack==PlayerState{}.attack+2*(PlayerProgress::MaxLevel-1),"invalid saved values are clamped or dropped");

        PlayerState fresh; int freshAffinity=30; QuestLog freshQuests; freshQuests.done.insert(1);
        PlayerProgress::Apply(fresh,freshAffinity,freshQuests,Json());
        Check(freshQuests.done.empty() && freshQuests.active.empty(),"new character has no quests");
        Check(fresh.level==1 && fresh.gold==0 && freshAffinity==30 && SameItems(fresh,PlayerState{}),"never-saved character starts new");
        std::println("PASS: character progress and quests capture/apply round trip, stat rebuild, tamper clamping, new character");
    }

    static void Run()
    {
        using namespace NpcMcp;
        Check(!Integer(Json::parse(R"({"delta":18446744073709551615})"),"delta",-2,2),"unsigned overflow rejected");
        Check(Integer({{"delta",-2}},"delta",-2,2) && Integer({{"delta",uint64_t{2}}},"delta",-2,2),"valid signed and unsigned arguments");
        Check(g_npcShopConfig.Load("Shop.lua"),"Shop.lua loads");
        {
            const auto fixture=std::filesystem::temp_directory_path()/"TurtleShopInvalid.lua";
            std::ofstream(fixture)<<"ShopPotions={HealthPotion={minPrice=90,avgPrice=80,maxPrice=120},ManaPotion={minPrice=1,avgPrice=2,maxPrice=3}}";
            Check(!g_npcShopConfig.Load(fixture.string().c_str()) && !g_npcShopConfig.enabled,"min above average disables shop");
            std::filesystem::remove(fixture);
        }
        g_npcShopConfig=NpcShopConfig{}; // fixed prices below
        Room room;
        room.m_inGame=true; room.m_userCount=1; room.m_userSlots[0]=0;
        auto& player=room.m_playerStates[0]; player.ready=true; player.hp=37; player.gold=987654;
        auto& chat=room.m_npcConversations[0];
        const auto now=GetTickCount64();
        chat.Begin(std::string(32,'a'),"I am poor",now);
        auto exchange=[&](const Json& request)
        {
            Check(request.dump().size()<=MaxMessageBytes,"request packet bound");
            Json result;
            Check(room.HandleNpcMcp(request,result),"context resolves");
            if (!result.is_null()) Check(result.dump().size()<=MaxMessageBytes,"response packet bound");
            return result;
        };
        NpcAgent::Client client(chat.context,exchange);
        const auto tools=client.Initialize();
        Check(tools.size()==9,"MCP pagination discovers all tools");
        auto call=[&](const char* name,Json args=Json::object())
        { return client.Request("tools/call",{{"name",name},{"arguments",args}}); };
        auto result=call("get_player_condition");
        Check(result.dump().find("987654")==std::string::npos && result.dump().find("gold")==std::string::npos,"no wealth leaks");
        Check(Json::parse(result["content"][0]["text"].get<std::string>())["hp"]==37,"live HP");
        Check(call("get_player_condition",{{"player_id",99}})["isError"]==true,"cannot choose another player");
        Check(call("propose_price",{{"item_id",1},{"quantity",1},{"unit_price",1}})["isError"]==true,"price floor enforced");
        Check(chat.quote.is_null(),"rejected quote not stored");
        Check(call("propose_price",{{"item_id",1},{"quantity",2},{"unit_price",100}})["isError"]==false,"allowed personal quote");
        Check(player.gold==987654 && player.items[0].count==3,"tools never buy or grant items");
        call("propose_affinity_change",{{"delta",2}});
        Check(chat.relationship.affinity==25 && chat.proposedRelationship.affinity==27,"affinity staged until final reply");
        Check(call("propose_affinity_change",{{"delta",2}})["isError"]==true,"per-turn affinity limit");
        Json invalid={{"jsonrpc","2.0"},{"id",90},{"method","tools/call"},{"params",{{"_meta",{{"turtleIsland/context",std::string(32,'b')}}}}}};
        Check(!room.HandleNpcMcp(invalid,result),"foreign context refused");
        Check(!room.CompleteNpcChat(999,{{"context",chat.context},{"say","hello"},{"completed",true}}),"wrong player completion refused");
        const auto token=chat.context;
        Check(room.CompleteNpcChat(g_userPool.Get(0)->GetId(),{{"context",token},{"say","hello"},{"completed",false}}),"failed inference handled");
        Check(chat.relationship.affinity==25 && chat.quote.is_null(),"failure discards staged changes");
        Check(!room.CompleteNpcChat(g_userPool.Get(0)->GetId(),{{"context",token},{"say","late"},{"completed",true}}),"duplicate completion ignored");

        chat.Begin(std::string(32,'c'),"second turn",now);
        NpcAgent::Client agentClient(chat.context,exchange);
        int step=0;
        const auto reply=NpcAgent::Run(agentClient,"I have no money",NpcAgent::Memory{},[&](const std::string& prompt, bool)
        {
            Check(prompt.find("987654")==std::string::npos && prompt.find(chat.context)==std::string::npos,"no balance or capability in model context");
            if (step==0) Check(prompt.find("get_shop_catalog")!=std::string::npos && prompt.find("base_price")!=std::string::npos,"shop list preloaded before first inference");
            if (step++==0) return std::string(R"({"tool":"get_player_condition","arguments":{}})");
            Check(prompt.find("tool_observations")!=std::string::npos && prompt.find("37")!=std::string::npos,"tool result returned to model");
            return std::string(R"({"say":"Have a rest."})");
        });
        Check(step==2 && reply=="Have a rest.","inference-tool-reinference loop");
        chat.Begin(std::string(32,'c'),"second turn",now);
        {
            int serverCalls=0, inferences=0, sayOnlySteps=0;
            std::string lastPrompt;
            NpcAgent::Client loopClient(chat.context,[&](const Json& request)
            { if (request.value("method",std::string{})=="tools/call") ++serverCalls; return exchange(request); });
            const auto looped=NpcAgent::Run(loopClient,"price?",NpcAgent::Memory{},[&](const std::string& prompt, bool sayOnly)
            {
                ++inferences; lastPrompt=prompt;
                if (sayOnly) { ++sayOnlySteps; return std::string(R"({"say":"Here you go."})"); }
                return std::string(R"({"tool":"get_player_condition","arguments":{}})");
            });
            Check(looped=="Here you go." && sayOnlySteps==1,"a stuck model ends with a forced reply instead of failing");
            Check(serverCalls==2,"repeated identical tool call is not sent to the server again");
            Check(inferences==5,"repeats are cut off without running the loop forever");
            Check(lastPrompt.find("You already had this result")!=std::string::npos,"a repeat gets the earlier result back, not a refusal");
            Check(lastPrompt.find(R"("arguments":{})")!=std::string::npos,"observations show the arguments used");
        }
        chat.Begin(std::string(32,'c'),"second turn",now);
        {
            // A repeat reached no server, so it must not spend budget: a model that stumbles on a
            // preloaded list still has the steps it needs to reach an acting tool.
            int scripted=0, conditionCalls=0;
            NpcAgent::Client stumbleClient(chat.context,[&](const Json& request)
            {
                if (request.value("method",std::string{})=="tools/call" &&
                    NpcMcp::Text(request["params"],"name",64)=="get_player_condition") ++conditionCalls;
                return exchange(request);
            });
            const auto stumbled=NpcAgent::Run(stumbleClient,"price?",NpcAgent::Memory{},[&](const std::string&, bool sayOnly)
            {
                if (sayOnly) return std::string(R"({"say":"Fine."})");
                if (++scripted<=2) return std::string(R"({"tool":"get_shop_catalog","arguments":{}})"); // already preloaded
                return std::string(R"({"tool":"get_player_condition","arguments":{}})");
            });
            Check(stumbled=="Fine." && conditionCalls==1,"repeats leave room for a later tool call");
        }
        {
            // Ids must never repeat across turns: a reply that arrives late would otherwise match a
            // later turn's request and be handed to the wrong caller.
            std::vector<std::string> ids;
            const auto echo=[&](const Json& request)
            {
                if (!request.contains("id")) return Json{};
                ids.push_back(request["id"].dump());
                return Json{{"jsonrpc","2.0"},{"id",request["id"]},{"result",Json::object()}};
            };
            for (int turn=0;turn<2;++turn)
            {
                NpcAgent::Client turnClient(chat.context,echo);
                turnClient.Request("tools/call",{{"name","get_relationship"}});
                turnClient.Request("tools/call",{{"name","get_relationship"}});
            }
            auto sorted=ids;
            std::sort(sorted.begin(),sorted.end());
            Check(ids.size()==4 && std::unique(sorted.begin(),sorted.end())==sorted.end(),"MCP ids are unique across turns");
        }
        NpcAgent::Memory memory;
        memory.turns=Json::array({{{"customer","oldest-turn"},{"npc","a"}},{{"customer","middle-turn"},{"npc","b"}},{{"customer","newest-turn"},{"npc","c"}}});
        memory.notes="- likes-blue";
        std::string trimmed;
        NpcAgent::Run(agentClient,"remember me",memory,[&](const std::string& prompt, bool) { trimmed=prompt; return std::string(R"({"say":"ok"})"); },
            [](const std::string& prompt) { return prompt.find("oldest-turn")==std::string::npos; });
        Check(trimmed.find("oldest-turn")==std::string::npos && trimmed.find("middle-turn")!=std::string::npos &&
            trimmed.find("newest-turn")!=std::string::npos,"over-budget prompt drops only the oldest dialogue");
        Check(trimmed.find("memory_notes")!=std::string::npos && trimmed.find("likes-blue")!=std::string::npos,"memory notes reach the model");

        Check(!NpcAgent::Compress(memory,[](const std::string&) { return std::string("- x"); }),"short dialogue is not compressed");
        for (int i=0;i<5;++i) memory.turns.push_back({{"customer","turn-"+std::to_string(i)},{"npc","n"}});
        std::string summaryInput;
        Check(!NpcAgent::Compress(memory,[](const std::string&) { return std::string{}; }) && memory.turns.size()==8,"failed summary keeps every turn");
        Check(NpcAgent::Compress(memory,[&](const std::string& input) { summaryInput=input; return std::string("- likes-blue, bought potion"); }),"close compresses");
        Check(summaryInput.find("likes-blue")!=std::string::npos && summaryInput.find("oldest-turn")!=std::string::npos &&
            summaryInput.find("middle-turn")!=std::string::npos && summaryInput.find("newest-turn")==std::string::npos,"summary folds previous notes and only the oldest turns");
        Check(memory.turns.size()==NpcAgent::RecentTurnsKept && memory.turns[0]["customer"]=="newest-turn" &&
            memory.notes=="- likes-blue, bought potion","recent turns stay verbatim beside the new notes");
        const Json repeated={{"jsonrpc","2.0"},{"id",17},{"method","tools/call"},{"params",{{"name","propose_affinity_change"},{"arguments",{{"delta",2}}}}}};
        auto first=chat.Dispatch(repeated,player,now);
        auto again=chat.Dispatch(repeated,player,now);
        Check(first==again && chat.proposedRelationship.affinity==27,"request ID replay does not apply twice");
        Check(room.CompleteNpcChat(g_userPool.Get(0)->GetId(),{{"context",chat.context},{"say","hello"},{"completed",true}}),"successful turn completes");
        Check(chat.relationship.affinity==27,"successful turn commits relationship");
        chat.Begin(std::string(32,'d'),"second turn",now+60001);
        Check(chat.Call("propose_affinity_change",{{"delta",2}},player,now+60001)["isError"]==true,"same message cannot earn affinity again");
        chat.Begin(std::string(32,'d'),"different turn",now+1);
        Check(chat.Call("propose_affinity_change",{{"delta",2}},player,now+1)["isError"]==true,"cooldown persists across turns");
        chat.Begin(std::string(32,'d'),"price then dislike",now+60001);
        Check(chat.Call("propose_price",{{"item_id",1},{"quantity",1},{"unit_price",92}},player,now+60001)["isError"]==false,"affinity discount quote");
        chat.Call("propose_affinity_change",{{"delta",-2}},player,now+60001);
        Check(chat.quote.is_null(),"quote invalidated when new floor exceeds price");

        {
            Check(LoadMonsterScript("Monster.lua") && g_monsterDefs.size()>=5,"Monster.lua loads");
            NpcConversation guide;
            guide.Begin(std::string(32,'m'),"tell me about monsters",now);
            auto text=[](const Json& result) { return Json::parse(result["content"][0]["text"].get<std::string>()); };
            const auto listed=guide.Call("get_monster_info",Json::object(),player,now);
            Check(Result(1,listed).dump().size()<=MaxMessageBytes && text(listed)["monsters"].size()==g_monsterDefs.size(),"monster list fits one message");
            for (size_t i=0;i<g_monsterDefs.size();++i)
            {
                const auto detail=guide.Call("get_monster_info",{{"monster_id",i}},player,now);
                const auto info=text(detail);
                Check(Result(1,detail).dump().size()<=MaxMessageBytes,"each monster's lore fits one MCP message");
                Check(info["name"]==g_monsterDefs[i].name && info["hunter_reward_gold"]==g_monsterDefs[i].gold &&
                    info.contains("habitat") && info.contains("looks") && info.contains("lore"),"monster lore and rewards come from Monster.lua");
            }
            Check(text(guide.Call("get_monster_info",{{"monster_id",0}},player,now))["danger_for_customer"]=="easy" &&
                text(guide.Call("get_monster_info",{{"monster_id",g_monsterDefs.size()-1}},player,now))["danger_for_customer"]=="deadly",
                "danger follows the customer's stats");
            Check(guide.Call("get_monster_info",{{"monster_id",200}},player,now)["isError"]==true,"unknown monster refused");
        }

        {
            // Pool users have no slot index in tests, so the buyer's state lives under that key.
            User buyer;
            auto& deal=room.m_npcConversations[buyer.GetSlotIndex()];
            auto& wallet=room.m_playerStates[buyer.GetSlotIndex()]; wallet=player;
            deal.relationship=chat.relationship;
            deal.Begin(std::string(32,'e'),"bulk deal",GetTickCount64());
            Check(deal.Call("propose_price",{{"item_id",1},{"quantity",2},{"unit_price",95}},wallet,now+60001)["isError"]==false,"bulk quote");
            CS_ShopBuy buy{}; buy.lifeId=wallet.lifeId; buy.itemId=1; buy.quantity=1; buy.unitPrice=95;
            const int gold=wallet.gold;
            room.BuyItem(buyer,buy);
            Check(wallet.gold==gold,"no purchase while the reply may still change the quote");
            deal.context.clear(); deal.previousQuote=deal.quote; // committed turn
            room.BuyItem(buyer,buy);
            Check(wallet.gold==gold-95 && wallet.items[0].count==4 && deal.quote["quantity"]==1,"negotiated purchase consumes quote");
            deal.Begin(std::string(32,'f'),"thanks",0);
            deal.Call("propose_price",{{"item_id",1},{"quantity",9},{"unit_price",110}},wallet,now+60001);
            Check(!deal.Busy(deal.expiresAt) && deal.quote["quantity"]==1,"unfinished turn keeps the committed quote");
            buy.quantity=2; room.BuyItem(buyer,buy);
            Check(wallet.gold==gold-95,"quote price limited to quoted quantity");
            buy.quantity=1; room.BuyItem(buyer,buy);
            Check(wallet.gold==gold-190 && deal.quote.is_null(),"quote used up");
            room.BuyItem(buyer,buy);
            Check(wallet.gold==gold-190,"stale quote price rejected");
            buy.unitPrice=100; wallet.gold=99; room.BuyItem(buyer,buy);
            Check(wallet.gold==99 && wallet.items[0].count==5,"not enough gold");
            wallet.gold=100000; buy.itemId=2; buy.quantity=99; wallet.items={{{1,99},{2,99},{1,99},{2,99},{1,99},{2,99}}};
            room.BuyItem(buyer,buy);
            Check(wallet.gold==100000,"full inventory rejected");

            Check(g_npcShopConfig.Load("Shop.lua") && g_npcShopConfig.Find(1008) && g_npcShopConfig.Find(1008)->equipment,"Shop.lua equipment loads");
            {
                const auto& rules=g_npcShopConfig.affinity;
                NpcConversation friendly;
                Check(friendly.relationship.affinity==rules.start,"Shop.lua sets starting affinity");
                friendly.Begin(std::string(32,'h'),"hello",1000);
                Check(friendly.Call("propose_affinity_change",{{"delta",rules.maxStep}},wallet,1000)["isError"]==false &&
                    friendly.proposedRelationship.affinity==std::min(100,rules.start+rules.maxStep),"Shop.lua allows the full step");
                friendly.relationship=friendly.proposedRelationship; friendly.context.clear();
                friendly.Begin(std::string(32,'i'),"thank you",1000+rules.cooldownMs);
                Check(friendly.Call("propose_affinity_change",{{"delta",rules.maxStep}},wallet,1000+rules.cooldownMs)["isError"]==false,
                    "Shop.lua cooldown allows the next friendly turn");
                Check(Tools(rules.maxStep)[4].dump().find(std::to_string(rules.maxStep))!=std::string::npos,"tool schema advertises the step");
            }
            deal.Begin(std::string(32,'g'),"catalog",0);
            const Json list={{"jsonrpc","2.0"},{"id",1},{"method","tools/call"},{"params",{{"name","get_shop_catalog"},{"arguments",Json::object()}}}};
            deal.ready=true;
            Check(deal.Dispatch(list,wallet,1).dump().size()<=MaxMessageBytes,"full catalog fits one MCP frame");
            Check(deal.Call("get_shop_catalog",{{"item_id",1008}},wallet,1).dump().find("minimum_price")!=std::string::npos,"per-item offer");
            Check(deal.Call("get_shop_catalog",{{"item_id",1008}},wallet,1).dump().find("move_speed")!=std::string::npos,"equipment offer includes stats");
            Check(deal.Call("get_shop_catalog",{{"item_id",999}},wallet,1)["isError"]==true,"unknown item rejected");
            deal.context.clear(); deal.quote=deal.previousQuote=nullptr;
            wallet.items={{{1,3}}}; wallet.gold=1000;
            const auto before=wallet;
            CS_ShopBuy gear{}; gear.lifeId=wallet.lifeId; gear.itemId=1003; gear.quantity=1; gear.unitPrice=850;
            room.BuyItem(buyer,gear);
            Check(wallet.gold==150 && wallet.items[1].itemId==1003 && wallet.items[1].count==1 && wallet.maxHP==before.maxHP+100 &&
                wallet.hp==before.hp+100 && wallet.attack==before.attack+40 && wallet.moveSpeed==before.moveSpeed+100,"equipment adds stats while held");
            gear.itemId=1002; gear.unitPrice=200; gear.quantity=2; wallet.gold=1000;
            room.BuyItem(buyer,gear);
            Check(wallet.gold==1000,"equipment is bought one per slot");
            g_npcShopConfig=NpcShopConfig{};
            room.m_npcConversations.erase(buyer.GetSlotIndex()); room.m_playerStates.erase(buyer.GetSlotIndex());
        }
        {
            g_questDefs={{1,"스토커 퇴치","풀숲의 스토커",0,2,100,50,1,0},{2,"후속 의뢰","다음 일",0,1,10,10,1,1}};
            auto& log=room.m_questLogs[0]; log={};
            const auto userId=g_userPool.Get(0)->GetId();
            auto turn=[&](char token,const char* say,bool completed)
            { return room.CompleteNpcChat(userId,{{"context",std::string(32,token)},{"say",say},{"completed",completed}}); };
            auto view=[&]() { return Json::parse(chat.Call("get_quests",Json::object(),player,now,&log)["content"][0]["text"].get<std::string>()); };
            chat.Begin(std::string(32,'q'),"any work?",now);
            Check(view()["quests"].size()==1 && view()["quests"][0]["status"]=="available","only unlocked quests are offered");
            Check(chat.Call("get_quests",Json::object(),player,now)["isError"]==true,"quest tools need the room's quest log");
            Check(chat.Call("accept_quest",{{"quest_id",2}},player,now,&log)["isError"]==true,"locked quest cannot be accepted");
            Check(chat.Call("accept_quest",{{"quest_id",1}},player,now,&log)["isError"]==false && log.active.empty() &&
                view()["quests"][0]["status"]=="active","accept is staged until the reply is delivered");
            Check(turn('q',"ok",true) && log.active.contains(1),"delivered reply commits the accept");
            Check(!log.OnKill(1) && log.OnKill(0) && log.OnKill(0) && !log.OnKill(0) && log.active[1]==2,"kills of the target count up to the goal");
            const int gold=player.gold;
            chat.Begin(std::string(32,'r'),"done",now);
            Check(chat.Call("complete_quest",{{"quest_id",1}},player,now,&log)["isError"]==false && player.gold==gold,"completion is staged");
            Check(turn('r',"",false) && log.active.contains(1) && player.gold==gold,"failed reply pays nothing");
            chat.Begin(std::string(32,'s'),"done",now);
            chat.Call("complete_quest",{{"quest_id",1}},player,now,&log);
            Check(turn('s',"thanks",true) && log.done.contains(1) && player.gold==gold+100,"delivered reply pays the reward");
            Check(view()["quests"][0]["quest_id"]==2 && chat.Call("complete_quest",{{"quest_id",1}},player,now,&log)["isError"]==true,
                "a finished quest unlocks the next one and cannot pay twice");
            SC_QuestState packet{};
            Check(sizeof(packet)<=MAX_SOCKBUF,"quest packet fits a socket frame");
        }
        chat.expiresAt=now;
        Check(chat.Dispatch(repeated,player,now).contains("error"),"expiry precedes replay");
        room.Leave(0);
        Check(room.m_npcConversations.empty(),"room leave clears capabilities and relationship session");
        std::println("PASS: MCP lifecycle/pagination, scope, HP privacy, price floor, staged affinity, replay, expiry and agent loop");
        TestProgress();
    }
};

static bool ReceiveAll(SOCKET socket, char* data, int length)
{
    while (length>0) { const int count=recv(socket,data,length,0); if (count<=0) return false; data+=count; length-=count; }
    return true;
}
static void SendAll(SOCKET socket,const std::vector<char>& data)
{
    size_t offset=0;
    while (offset<data.size()) { const int count=send(socket,data.data()+offset,static_cast<int>(data.size()-offset),0); Check(count>0,"socket send"); offset+=count; }
}
static void LiveAi()
{
    using namespace NpcMcp;
    WSADATA data{}; Check(WSAStartup(MAKEWORD(2,2),&data)==0,"Winsock startup");
    const SOCKET socket=::socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(4446); address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    Check(connect(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"connect test AI on port 4446");
    const DWORD timeout=180000;
    setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    NpcConversation chat; chat.Begin(std::string(32,'f'),"live test",GetTickCount64());
    GameplayRules::PlayerState player; player.ready=true; player.hp=37; player.gold=987654;
    const auto body=Json{{"context",chat.context},{"conversation",std::string(32,'e')},
        {"say","지금 내 체력이 얼마나 남았는지 확인해줘. 그리고 HP 포션 두 개를 살 때 허용된 최저 가격으로 제안해줘."}}.dump();
    std::vector<char> packet(sizeof(SA_AI)+body.size());
    auto* header=reinterpret_cast<SA_AI*>(packet.data());
    header->size=static_cast<uint16_t>(packet.size()); header->id=static_cast<uint16_t>(PACKET_ID::SA_AI_AGENT); header->userIndex=7; header->npcIndex=0;
    std::memcpy(packet.data()+sizeof(SA_AI),body.data(),body.size()); SendAll(socket,packet);
    bool condition=false,quoted=false,completed=false;
    for (int frame=0;frame<32;++frame)
    {
        PacketHeader h{}; Check(ReceiveAll(socket,reinterpret_cast<char*>(&h),sizeof(h)),"AI response timeout/disconnect");
        Check(h.size>=sizeof(h) && h.size<=1024,"AI packet size");
        std::string payload(h.size-sizeof(h),'\0'); Check(ReceiveAll(socket,payload.data(),static_cast<int>(payload.size())),"AI frame body");
        if (h.id==static_cast<uint16_t>(PACKET_ID::AS_AI_AGENT))
        {
            Check(payload.size()>=sizeof(uint32_t),"final header");
            const auto final=Json::parse(payload.substr(sizeof(uint32_t)));
            std::println("AI final: {}",final["say"].get<std::string>());
            completed=final.value("completed",false); break;
        }
        Check(h.id==static_cast<uint16_t>(PACKET_ID::AS_MCP),"MCP request packet");
        const auto request=Json::parse(payload);
        const auto method=request.value("method",std::string{});
        if (method=="tools/call")
        {
            const auto name=request["params"]["name"].get<std::string>();
            std::println("LLM tool: {} {}",name,request["params"]["arguments"].dump());
            condition |= name=="get_player_condition";
        }
        auto response=chat.Dispatch(request,player,GetTickCount64());
        quoted |= !chat.quote.is_null();
        if (response.is_null()) continue;
        const auto text=response.dump();
        Check(text.find("987654")==std::string::npos,"live tool privacy");
        std::vector<char> output(sizeof(PacketHeader)+text.size());
        auto* out=reinterpret_cast<PacketHeader*>(output.data()); out->size=static_cast<uint16_t>(output.size()); out->id=static_cast<uint16_t>(PACKET_ID::SA_MCP);
        std::memcpy(output.data()+sizeof(PacketHeader),text.data(),text.size()); SendAll(socket,output);
    }
    closesocket(socket); WSACleanup();
    Check(completed && condition && quoted,"real model completed HP lookup and valid price proposal");
    std::println("PASS: real local LLM -> MCP tools -> authoritative fixture -> final dialogue");
}

int main(int argc,char** argv)
{
    try { if (argc>1 && std::string(argv[1])=="--ai-live") LiveAi(); else NpcMcpTests::Run(); return 0; }
    catch (const std::exception& e) { std::println("FAIL: {}",e.what()); return 1; }
}
