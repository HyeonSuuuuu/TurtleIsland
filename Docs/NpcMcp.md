# NPC MCP Agent

## Scope

The existing NPC chat now runs a local-LLM tool loop. This first stage supports
condition lookup, catalog lookup, relationship lookup, price proposals and small
relationship changes. The model never purchases. The player buys from the shop
list (`CS_SHOP_BUY`), paying the base price or the committed quote, which the
server re-checks. Gold, inventory and relationships are not persisted in the
database. Only the existing shop NPC ID `0` is supported, with no distance check.

## Purchase

- `SC_SHOP_PRICES` (owner only) carries each potion's current unit price and the
  remaining quoted quantity. It is sent on ready, respawn, each completed NPC
  turn and each purchase.
- `CS_SHOP_BUY {lifeId, itemId, quantity, unitPrice}` must echo that price. A
  mismatch returns `PriceChanged`. Other results are `NotEnoughGold`,
  `InventoryFull` (stack limit 99) and `Cooldown`, which means an NPC turn is
  still pending and could change the quote.
- A quote applies to up to its quantity, persists across later turns and is
  consumed by purchases. A failed or expired turn restores the previous quote.
  Death clears it.
- The client wraps each `Frame*` cell in `WBP_Shop` that contains exactly one
  red or blue potion icon in a buy button, and writes the price into the cell's
  TextBlock.

## Architecture

```text
Unreal NPC chat
  -> GameServer: validate player and issue a short-lived conversation capability
  -> AI server: MCP client + local llama.cpp inference host
  -> GameServer: MCP tools, authoritative validation, structured result
  -> AI server: feed result into the next inference
  -> GameServer: commit accepted conversation changes and append verified quote
  -> Unreal: existing NPC chat display
```

The model chooses a tool name and JSON arguments. It does not open sockets or
construct game packets. `AI/Server/Server/NpcAgent.h` implements the MCP client
inside the existing AI process; no separate client application is needed.

This uses MCP JSON-RPC 2.0, protocol version `2025-11-25`, lifecycle initialization,
`tools/list` pagination and `tools/call` over a **custom transport**: the existing
binary game/AI TCP connection. It is not an HTTP or stdio MCP endpoint. Generic
MCP clients need a transport adapter. See the official specifications:

- https://modelcontextprotocol.io/specification/2025-11-25/basic/transports
- https://modelcontextprotocol.io/specification/2025-11-25/server/tools

Each frame is at most 1024 bytes, including the existing 4-byte packet header.
`AS_MCP` (3003) carries one JSON-RPC request/notification; `SA_MCP` (3103)
carries its response. `SA_AI_AGENT` (3102) reuses the `SA_AI` header with a JSON
envelope containing `context`, `conversation`, and `say`. `AS_AI_AGENT` (3002)
reuses the `AS_AI` header with `context`, `say`, and `completed`. Existing client
chat packets and legacy AI packet layouts are unchanged. Upgrade both server
executables together; the game server now sends the agent form.

## Tools And Privacy

| Tool | Allowed effect |
| --- | --- |
| `get_player_condition` | Read HP, maximum HP and level only |
| `get_shop_catalog` | Read two potion products and their allowed prices |
| `get_relationship` | Read this player's affinity with this NPC |
| `propose_price` | Stage a quantity and unit-price quote; never buy |
| `propose_affinity_change` | Stage a server-bounded change between -2 and +2 |

Gold, inventory, account identifiers and other NPC relationships are excluded
from model context and tool results. The model can hear a player's claim about
wealth but cannot verify it. Tool arguments cannot select another player or NPC.
Only the host supplies the capability through `params._meta["turtleIsland/context"]`;
neither capability nor internal identity is included in the inference prompt.

Model output is constrained with a JSON grammar and checked again by the host.
Prompts are not a security boundary: the game server independently validates all
tool arguments and allowed prices. Dialogue can still be inaccurate; only the
server-appended quote is authoritative. No model response can transfer money or
grant an item in this implementation.

## Policy And Configuration

`Server/Server/Shop.lua` is copied beside the game-server executable by the
build. Run the game server with that directory as its working directory. Missing
or invalid config disables the shop (conversation and purchase) and logs a
warning. Restart after editing the file. Item IDs 1 and 2 are the health and
mana potions (`ShopPotions.HealthPotion`, `ShopPotions.ManaPotion`).

- `minPrice`, `avgPrice` and `maxPrice` are integers, with
  `minPrice <= avgPrice <= maxPrice`, all between 1 and 100000. `avgPrice` is
  the list price. Both potions are required.
- Affinity starts at 25 and is clamped to 0..100.
- Current unit-price floor is `avgPrice - floor((avgPrice-minPrice)*affinity/100)`.
  With 100/70/120 and affinity 25, the floor is 93G, never below 70G.
- Quantity is 1..99. A later affinity decrease invalidates an under-floor quote.
- At most one nonzero affinity change per turn, a 60-second cooldown between
  successful changes, and at most +10 per five-minute window. The last 32 rewarded
  messages cannot earn another increase by exact repetition. Paraphrases are not
  detected; the cooldown and gain cap still apply.
- Changes are staged until a successful final reply. Failed inference discards
  them. Duplicate request IDs return cached results, not repeated effects.
- Conversation capabilities expire after 120 seconds and are invalidated on
  completion/room leave. Dead or unready players cannot use tools or receive a
  late completed turn. One pending turn per player; six tool calls per turn.
- Relationships live only in the room session and reset on leave/restart.
- AI history is bounded to four dialogue turns and 4000 bytes per conversation,
  up to 128 conversations per game-server connection. History eviction does not
  grant the model access to any other conversation.

The AI listener binds to `127.0.0.1`, port 4444 by default. Start AI before the
game server. `TI_AI_PORT` can override the AI port for tests; the game server's
configured destination must match. The transport has no remote authentication
or TLS, so do not expose it directly on an untrusted network. The current design
assumes both services run on the same trusted machine. A hosted/distributed
deployment needs authenticated transport before widening the bind address.

## Verification

### Model Location

The AI server resolves its GGUF independently of the launch working directory.
Set `TI_AI_MODEL` to an absolute GGUF file path to explicitly select a model. An
invalid override fails with a list of checked paths; it never silently chooses
another model. Without an override, it searches `models` beside the executable,
then `models` in the working directory, then the source project's and solution's
`models`, `x64/Release/models`, and `x64/Debug/models` directories discovered
through both the executable's and working directory's parent directories.
The default filename remains
`gemma-4-E4B-it-Q4_K_M.gguf`; model files are not downloaded or copied by the build.

Run `Server.exe --check-model-path` to print the resolved file without loading
the model or opening a listening port. A missing file and a model load failure
(for example, insufficient memory or an unsupported GGUF) now have distinct errors.

For Visual Studio, open `AI/Server/Server.sln`, select the `Server` startup project,
and use **Debug x64** or **Release x64** with F5. Both configurations use the
required C++ language standard and set the debugger working directory to
`$(ProjectDir)`. The existing model can stay in
`AI/Server/Server/x64/Release/models`; a Debug build finds it without another copy.
Stop any previously launched AI server before F5, because the default port 4444
cannot be bound by two processes. An explicit `TI_AI_MODEL` environment variable
still takes priority over all automatic paths.

### Tests

Build `Server/Server/ServerMcpTests.vcxproj` in Debug x64 and run
`ServerMcpTests.exe`. It tests lifecycle/pagination, strict numeric bounds, scope,
HP-only context, server price floors, staged affinity commit/rollback, cooldown,
repeat rejection, replay, expiry and the inference/tool/re-inference loop.

For the optional real-model smoke test, start the AI executable with
`TI_AI_PORT=4446`, from a directory containing its model, then run
`ServerMcpTests.exe --ai-live`. This test uses an isolated authoritative fixture,
not a live player or database. It requires a successful HP lookup and valid
two-potion quote from the real LLM. On 2026-09-12 the local Gemma model called
`get_player_condition`, `get_shop_catalog`, and `propose_price` (2 HP potions at
93G), then produced its final Korean response. This does not replace an Unreal
client end-to-end test or multi-user load testing. The synchronous agent loop
currently serializes turns on a game-server connection; a busy production server
will need bounded concurrent scheduling and explicit queue-timeout UX.
