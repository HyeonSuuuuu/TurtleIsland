#include "pch.h"
#include "Room.h"
#include "Globals.h"
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
        Check(tools.size()==5,"MCP pagination discovers all tools");
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
        const auto reply=NpcAgent::Run(agentClient,"I have no money",Json::array(),[&](const std::string& prompt)
        {
            Check(prompt.find("987654")==std::string::npos && prompt.find(chat.context)==std::string::npos,"no balance or capability in model context");
            if (step++==0) return std::string(R"({"tool":"get_player_condition","arguments":{}})");
            Check(prompt.find("tool_observations")!=std::string::npos && prompt.find("37")!=std::string::npos,"tool result returned to model");
            return std::string(R"({"say":"Have a rest."})");
        });
        Check(step==2 && reply=="Have a rest.","inference-tool-reinference loop");
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
            room.m_npcConversations.erase(buyer.GetSlotIndex()); room.m_playerStates.erase(buyer.GetSlotIndex());
        }
        chat.expiresAt=now;
        Check(chat.Dispatch(repeated,player,now).contains("error"),"expiry precedes replay");
        room.Leave(0);
        Check(room.m_npcConversations.empty(),"room leave clears capabilities and relationship session");
        std::println("PASS: MCP lifecycle/pagination, scope, HP privacy, price floor, staged affinity, replay, expiry and agent loop");
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
