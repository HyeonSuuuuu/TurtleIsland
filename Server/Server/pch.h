#pragma once

// Windows / Winsock (순서 중요: winsock2 먼저)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX               // windows.h 의 min/max 매크로가 std::min/std::max 를 깨뜨리지 않도록
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

// STL
#include <cstdint>
#include <array>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>
#include <climits>
#include <print>
#include <iostream>

#include "Log.h"
