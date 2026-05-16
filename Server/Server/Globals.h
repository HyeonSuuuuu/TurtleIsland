#pragma once
#include "pch.h"
#include "EBR.h"
#include "User.h"
#include "../../Common/Protocol.h"

inline std::unordered_map<std::string, uint32_t> g_idToIndexMap{};
inline EBRPool<User, NetConfig::MAX_CLIENTS>      g_user_pool;
