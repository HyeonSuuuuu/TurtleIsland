#include "pch.h"
#include "Room.h"
#include "Globals.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

namespace
{
    std::string Nonce()
    {
        std::array<unsigned char,16> bytes{};
        if (BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
            throw std::runtime_error("NPC context RNG failed");
        const char* hex="0123456789abcdef";
        std::string result;
        for (const auto b:bytes) { result+=hex[b>>4]; result+=hex[b&15]; }
        return result;
    }
    void Say(User& user, const std::string& text)
    {
        if (text.size()>MAX_SOCKBUF-sizeof(SC_NpcChat)) return;
        std::vector<uint8_t> data(sizeof(SC_NpcChat)+text.size());
        auto* header=reinterpret_cast<SC_NpcChat*>(data.data());
        header->size=static_cast<uint16_t>(data.size()); header->id=static_cast<uint16_t>(PACKET_ID::SC_NPC_CHAT);
        std::memcpy(data.data()+sizeof(SC_NpcChat),text.data(),text.size());
        user.Send(data);
    }
}

void Room::BeginNpcChat(User& user, uint32_t npcId, const std::string& message)
{
    std::lock_guard lock(m_mutex);
    const auto found=m_playerStates.find(user.GetSlotIndex());
    if (!g_npcShopConfig.enabled || !m_inGame || found==m_playerStates.end() || !found->second.ready || found->second.hp<=0 || npcId!=0)
    { Say(user,"지금은 이 상인과 대화할 수 없습니다."); return; }
    if (!g_aiSession) { Say(user,"상인이 잠시 대답할 수 없습니다."); return; }
    if (message.empty() || message.size()>600) { Say(user,"대화는 UTF-8 600바이트 이내로 입력해주세요."); return; }
    const auto now=GetTickCount64();
    auto& chat=m_npcConversations[user.GetSlotIndex()];
    if (!chat.context.empty() && now<chat.expiresAt) { Say(user,"상인의 답변을 기다려주세요."); return; }
    try
    {
        if (chat.conversation.empty()) chat.conversation=Nonce();
        chat.Begin(Nonce(),message,now);
        const auto payload=NpcMcp::Json{{"context",chat.context},{"conversation",chat.conversation},{"say",message}}.dump();
        if (payload.size()+sizeof(SA_AI)>MAX_SOCKBUF) { chat.context.clear(); Say(user,"대화가 너무 깁니다."); return; }
        std::vector<uint8_t> data(sizeof(SA_AI)+payload.size());
        auto* packet=reinterpret_cast<SA_AI*>(data.data());
        packet->size=static_cast<uint16_t>(data.size()); packet->id=static_cast<uint16_t>(PACKET_ID::SA_AI_AGENT);
        packet->userIndex=user.GetId(); packet->npcIndex=npcId;
        std::memcpy(data.data()+sizeof(SA_AI),payload.data(),payload.size());
        g_aiSession->Send(data);
    }
    catch (const std::exception&) { chat.context.clear(); Say(user,"대화를 시작할 수 없습니다."); }
}

bool Room::HandleNpcMcp(const NpcMcp::Json& request, NpcMcp::Json& response)
{
    using namespace NpcMcp;
    if (!request.is_object()) return false;
    const auto params=request.value("params",Json::object());
    if (!params.is_object()) return false;
    const auto meta=params.value("_meta",Json::object());
    const auto context=Text(meta,"turtleIsland/context",32);
    if (context.size()!=32) return false;
    std::lock_guard lock(m_mutex);
    for (auto& [slot,chat]:m_npcConversations)
    {
        if (chat.context!=context) continue;
        const auto player=m_playerStates.find(slot);
        if (!m_inGame || player==m_playerStates.end()) return false;
        response=chat.Dispatch(request,player->second,GetTickCount64());
        return true;
    }
    return false;
}

bool Room::CompleteNpcChat(uint32_t playerId, const NpcMcp::Json& response)
{
    using namespace NpcMcp;
    const auto context=Text(response,"context",32);
    const auto say=Text(response,"say",700);
    if (context.size()!=32) return false;
    std::lock_guard lock(m_mutex);
    for (auto& [slot,chat]:m_npcConversations)
    {
        if (chat.context!=context) continue;
        auto* user=g_userPool.Get(slot);
        const auto player=m_playerStates.find(slot);
        if (!user || user->GetId()!=playerId || player==m_playerStates.end()) return false;
        chat.context.clear(); // Duplicate and late responses cannot be delivered to another turn.
        if (!m_inGame || !player->second.ready || player->second.hp<=0 || GetTickCount64()>=chat.expiresAt) return true;
        const bool completed=response.contains("completed") && response["completed"]==true && !say.empty();
        if (completed) chat.relationship=chat.proposedRelationship;
        else chat.quote=nullptr;
        std::string text=say.empty()?"상인이 대답을 정리하지 못했습니다. 다시 말씀해주세요.":say;
        if (!chat.quote.is_null())
            text += "\n[서버 확인 가격] " + std::string(chat.quote["item_id"]==1?"HP 포션":"MP 포션") + " " +
                std::to_string(chat.quote["quantity"].get<int>()) + "개, 개당 " + std::to_string(chat.quote["unit_price"].get<int>()) +
                "G (구매되지 않음)";
        Say(*user,text);
        return true;
    }
    return false;
}
