#pragma once

// Windows / Winsock (순서 중요: winsock2 먼저)
#define WIN32_LEAN_AND_MEAN
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
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>
#include <print>
#include <functional>
#include <iostream>

// 공통 유틸
#include "../../Common/Log.h"
