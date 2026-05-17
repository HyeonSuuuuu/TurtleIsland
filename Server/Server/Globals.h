#pragma once
#include "pch.h"
#include "EBR.h"
#include "User.h"
#include "Room.h"
#include "../../Common/Protocol.h"

inline std::unordered_map<uint32_t, uint32_t>                          g_idToIndexMap{};
inline EBRPool<User, NetConfig::MAX_CLIENTS>                           g_userPool;
inline std::array<GameObject*, NetConfig::MAX_CLIENTS + MAX_NPCS>      g_objects{};
inline SendPool                                                        g_sendPool{};
inline std::array<Room, MAX_ROOMS>                                     g_rooms{};
inline Session*                                                        g_aiSession = nullptr;
