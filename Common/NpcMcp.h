#pragma once
#include "ThirdParty/nlohmann/json.hpp"
#include <string>
#include <array>

namespace NpcMcp
{
using Json = nlohmann::json;
inline constexpr const char* Version = "2025-11-25";
inline constexpr size_t MaxMessageBytes = 1020;
inline constexpr size_t MaxToolCalls = 6;
inline constexpr uint64_t TurnLifetimeMs = 120000;

inline Json Error(const Json& id, int code, const std::string& message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}
inline Json Result(const Json& id, const Json& result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
inline Json ToolResult(const Json& value, bool error = false)
{
    return {{"content", Json::array({{{"type", "text"}, {"text", value.dump()}}})}, {"isError", error}};
}
inline Json ToolError(const char* code)
{
    return ToolResult({{"error", code}}, true);
}
inline bool Integer(const Json& args, const char* key, int minimum, int maximum)
{
    if (!args.contains(key) || !args[key].is_number_integer()) return false;
    if (args[key].is_number_unsigned())
    {
        const auto value = args[key].get<uint64_t>();
        return maximum >= 0 && value <= static_cast<uint64_t>(maximum) &&
            (minimum <= 0 || value >= static_cast<uint64_t>(minimum));
    }
    const auto value = args[key].get<int64_t>();
    return value >= minimum && value <= maximum;
}
inline Json Tools()
{
    const Json empty = {{"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};
    const auto number = [](int low, int high) -> Json { return {{"type", "integer"}, {"minimum", low}, {"maximum", high}}; };
    return Json::array({
        {{"name", "get_player_condition"}, {"description", "Read this customer's HP, maximum HP and level. Wealth is unknown."}, {"inputSchema", empty}},
        {{"name", "get_shop_catalog"}, {"description", "Read products and currently allowed unit prices."}, {"inputSchema", empty}},
        {{"name", "get_relationship"}, {"description", "Read this customer's relationship with this NPC."}, {"inputSchema", empty}},
        {{"name", "propose_price"}, {"description", "Propose a personal quote. Does not purchase; the customer buys from the shop list."},
            {"inputSchema", {{"type", "object"}, {"properties", {{"item_id", number(1,2)}, {"quantity", number(1,99)}, {"unit_price", number(1,100000)}}},
                {"required", {"item_id", "quantity", "unit_price"}}, {"additionalProperties", false}}}},
        {{"name", "propose_affinity_change"}, {"description", "Suggest a small relationship change. Server may refuse repetition or cooldown."},
            {"inputSchema", {{"type", "object"}, {"properties", {{"delta", number(-2,2)}}}, {"required", {"delta"}}, {"additionalProperties", false}}}}
    });
}
inline std::string Text(const Json& value, const char* key, size_t maxBytes)
{
    if (!value.is_object() || !value.contains(key) || !value[key].is_string()) return {};
    auto result = value[key].get<std::string>();
    return result.size() <= maxBytes ? result : std::string{};
}
}
