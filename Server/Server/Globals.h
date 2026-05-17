#pragma once
#include "pch.h"
#include "EBR.h"
#include "User.h"
#include "../../Common/Protocol.h"

inline std::unordered_map<uint32_t, uint32_t>                          g_idToIndexMap{};
inline EBRPool<User, NetConfig::MAX_CLIENTS>                           g_userPool;
inline std::array<GameObject*, NetConfig::MAX_CLIENTS + MAX_NPCS>      g_objects{};
inline SendPool                                                        g_sendPool{};
