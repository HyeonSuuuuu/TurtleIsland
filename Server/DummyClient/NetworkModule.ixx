module;
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <Windows.h>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#pragma comment(lib, "ws2_32.lib")

export module NetworkModule;
import "../../Common/Protocol.h";

using namespace std;
using namespace chrono;

// -------------------------------------------------------
// Config
// -------------------------------------------------------
constexpr int  MAX_CLIENTS      = 20000; // hard upper bound (array size)
constexpr int  ROOM_SIZE        = 4;     // mirror server MAX_ROOM_USERS
constexpr int  CONNECT_DELAY_MS = 50;   // ms between new connections
constexpr int  MAX_BUFF_SIZE    = 1024;
constexpr int  MAX_PACKET_SIZE  = 1024;
constexpr int  INVALID_CID      = -1;

int g_maxTest = 200; // runtime target, set via SetMaxClients()

// -------------------------------------------------------
// Exported globals (read by DrawModule)
// -------------------------------------------------------
export atomic_int active_clients{ 0 };
export int        global_delay = 0;      // currently unused, kept for DrawModule

// -------------------------------------------------------
// Internal state
// -------------------------------------------------------
extern HWND hWnd;

char g_serverIP[64] = "127.0.0.1";

HANDLE g_hiocp;

enum OPTYPE { OP_SEND, OP_RECV };

enum class ClientState : uint8_t
{
	Connecting,
	InLobby,
	EnteringRoom,
	InRoom,
};

high_resolution_clock::time_point g_lastConnectTime;

struct OverlappedEx
{
	WSAOVERLAPPED over{};
	WSABUF        wsabuf{};
	unsigned char IOCP_buf[MAX_BUFF_SIZE]{};
	OPTYPE        event_type{};
};

struct CLIENT
{
	int         id       = INVALID_CID;
	uint32_t    playerId = 0;
	float       x = 0.0f, z = 88.0f;   // top-down 2D (x, z)
	uint32_t    roomIdx = UINT32_MAX;
	ClientState state = ClientState::Connecting;
	atomic_bool connected{ false };

	SOCKET       client_socket = INVALID_SOCKET;
	OverlappedEx recv_over{};

	unsigned char packet_buf[MAX_PACKET_SIZE]{};
	unsigned      prev_packet_data = 0;   // bytes already in packet_buf
	unsigned      curr_packet_size = 0;   // expected total size (0 = unknown)

	high_resolution_clock::time_point last_move_time{};
};

array<CLIENT, MAX_CLIENTS> g_clients{};
atomic_int                 num_connections{ 0 };

vector<thread*> g_workerThreads;
thread          g_testThread;

// -------------------------------------------------------
// Room visualization
// -------------------------------------------------------
export struct RoomView
{
	int   roomId = -1;
	int   count  = 0;
	float x[ROOM_SIZE]{};
	float z[ROOM_SIZE]{};
};

constexpr int MAX_ROOM_VIEWS = MAX_CLIENTS / ROOM_SIZE + 1;
RoomView g_roomViewBuf[MAX_ROOM_VIEWS]{};

float g_pointCloud[MAX_CLIENTS * 2]{};

// -------------------------------------------------------
// Helpers
// -------------------------------------------------------
void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf = nullptr;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, nullptr);
	wprintf(L"[NET ERROR] %S : %s\n", msg, lpMsgBuf ? lpMsgBuf : L"(unknown)");
	LocalFree(lpMsgBuf);
}

void DisconnectClient(int ci)
{
	if (ci < 0 || ci >= MAX_CLIENTS) return;
	bool expected = true;
	if (g_clients[ci].connected.compare_exchange_strong(expected, false))
	{
		closesocket(g_clients[ci].client_socket);
		g_clients[ci].client_socket = INVALID_SOCKET;
		active_clients--;
	}
}

// size comes from the first 2 bytes of the packet (PacketHeader.size)
void SendPacket(int cl, void* packet)
{
	uint16_t psize = *reinterpret_cast<uint16_t*>(packet);
	if (psize == 0 || psize > MAX_BUFF_SIZE) return;

	OverlappedEx* over = new OverlappedEx{};
	over->event_type = OP_SEND;
	memcpy(over->IOCP_buf, packet, psize);
	over->wsabuf.buf = reinterpret_cast<CHAR*>(over->IOCP_buf);
	over->wsabuf.len = psize;

	int ret = WSASend(g_clients[cl].client_socket, &over->wsabuf, 1,
		nullptr, 0, &over->over, nullptr);
	if (ret != 0 && WSAGetLastError() != WSA_IO_PENDING)
	{
		error_display("WSASend", WSAGetLastError());
		delete over;
	}
}

// -------------------------------------------------------
// Packet processing
// -------------------------------------------------------
void ProcessPacket(int ci, unsigned char* packet)
{
	auto*    header   = reinterpret_cast<PacketHeader*>(packet);
	auto     packetId = static_cast<PACKET_ID>(header->id);

	switch (packetId)
	{
	// ---------- Login ----------
	case PACKET_ID::SC_LOGIN_RESPONSE:
	{
		auto* p = reinterpret_cast<SC_Login*>(packet);
		if (p->result != static_cast<uint8_t>(ERROR_CODE::NONE)) break;

		g_clients[ci].playerId   = p->playerId;
		g_clients[ci].state      = ClientState::InLobby;
		g_clients[ci].connected  = true;
		active_clients++;

		// -> CS_LOBBY_ENTER_REQUEST
		CS_LobbyEnter lp{};
		lp.size = CS_LOBBY_ENTER_PACKET_SIZE;
		lp.id   = static_cast<uint16_t>(PACKET_ID::CS_LOBBY_ENTER_REQUEST);
		SendPacket(ci, &lp);
		break;
	}

	// ---------- Lobby ----------
	case PACKET_ID::SC_LOBBY_RESPONSE:
	{
		g_clients[ci].state = ClientState::EnteringRoom;

		// clients 0-3 -> room 0, 4-7 -> room 1, ...
		uint32_t roomIdx = static_cast<uint32_t>((ci / ROOM_SIZE) % 5000);
		g_clients[ci].roomIdx = roomIdx;

		CS_RoomEnter rp{};
		rp.size      = CS_ROOM_ENTER_PACKET_SIZE;
		rp.id        = static_cast<uint16_t>(PACKET_ID::CS_ROOM_ENTER_REQUEST);
		rp.roomIndex = roomIdx;
		SendPacket(ci, &rp);
		break;
	}

	// ---------- Room Enter ----------
	case PACKET_ID::SC_ROOM_ENTER_RESPONSE:
	{
		auto* p = reinterpret_cast<SC_RoomEnter*>(packet);
		if (p->result == static_cast<uint8_t>(ERROR_CODE::NONE))
			g_clients[ci].state = ClientState::InRoom;
		break;
	}

	// ---------- Move Notify (update position) ----------
	case PACKET_ID::SC_MOVE_NOTIFY:
	{
		auto* p = reinterpret_cast<SC_MoveNotify*>(packet);
		// Find the local client with this playerId and update position
		for (int i = 0; i < num_connections.load(); ++i)
		{
			if (g_clients[i].playerId == p->userIndex)
			{
				g_clients[i].x = p->x;
				g_clients[i].z = p->z;
				break;
			}
		}
		break;
	}

	// ---------- Silently ignored ----------
	case PACKET_ID::SC_ROOM_ENTER_NOTIFY:
	case PACKET_ID::SC_ROOM_LEAVE_NOTIFY:
	case PACKET_ID::SC_ROOM_CHAT_NOTIFY:
	case PACKET_ID::SC_ROOM_CHAT_RESPONSE:
	case PACKET_ID::SC_ROOM_START_NOTIFY:
	case PACKET_ID::SC_NPC_CHAT:
		break;

	default:
		break;
	}
}

// -------------------------------------------------------
// Worker thread (IOCP)
// -------------------------------------------------------
void Worker_Thread()
{
	while (true)
	{
		DWORD          io_size = 0;
		ULONG_PTR      ci      = 0;
		OverlappedEx*  over    = nullptr;

		BOOL ret = GetQueuedCompletionStatus(g_hiocp, &io_size, &ci,
			reinterpret_cast<LPWSAOVERLAPPED*>(&over), INFINITE);

		int client_id = static_cast<int>(ci);

		if (!ret || io_size == 0)
		{
			DisconnectClient(client_id);
			if (over && over->event_type == OP_SEND) delete over;
			continue;
		}

		if (over->event_type == OP_RECV)
		{
			unsigned char* buf     = g_clients[ci].recv_over.IOCP_buf;
			unsigned       psize   = g_clients[ci].curr_packet_size;
			unsigned       pr_size = g_clients[ci].prev_packet_data;

			while (io_size > 0)
			{
				// Need 2 bytes to determine packet size (PacketHeader.size = uint16_t)
				if (psize == 0)
				{
					if (pr_size < 2)
					{
						unsigned take = min(2u - pr_size, io_size);
						memcpy(g_clients[ci].packet_buf + pr_size, buf, take);
						pr_size += take;
						buf     += take;
						io_size -= take;
						if (pr_size < 2) break;
					}
					psize = *reinterpret_cast<uint16_t*>(g_clients[ci].packet_buf);
					if (psize == 0 || psize > MAX_PACKET_SIZE)
					{
						DisconnectClient(client_id);
						psize = 0; pr_size = 0; io_size = 0;
						break;
					}
				}

				unsigned need = psize - pr_size;
				unsigned take = min(need, io_size);
				memcpy(g_clients[ci].packet_buf + pr_size, buf, take);
				pr_size += take;
				buf     += take;
				io_size -= take;

				if (pr_size >= psize)
				{
					ProcessPacket(client_id, g_clients[ci].packet_buf);
					psize   = 0;
					pr_size = 0;
				}
			}

			g_clients[ci].curr_packet_size = psize;
			g_clients[ci].prev_packet_data = pr_size;

			DWORD recv_flag = 0;
			int recv_ret = WSARecv(g_clients[ci].client_socket,
				&g_clients[ci].recv_over.wsabuf, 1,
				nullptr, &recv_flag, &g_clients[ci].recv_over.over, nullptr);
			if (recv_ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
				DisconnectClient(client_id);
		}
		else if (over->event_type == OP_SEND)
		{
			delete over;
		}
	}
}

// -------------------------------------------------------
// Connect a new dummy client
// -------------------------------------------------------
void ConnectNewClient()
{
	int ci = num_connections.load();
	if (ci >= MAX_CLIENTS) return;

	SOCKET sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (sock == INVALID_SOCKET) return;

	// 네이글 비활성화: 서버와 동일하게 소형 패킷 지연을 막는다
	BOOL noDelay = TRUE;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, sizeof(noDelay));

	SOCKADDR_IN addr{};
	addr.sin_family           = AF_INET;
	addr.sin_port             = htons(NetConfig::GAME_SERVER_PORT);
	addr.sin_addr.s_addr      = inet_addr(g_serverIP);

	if (WSAConnect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr),
		nullptr, nullptr, nullptr, nullptr) != 0)
	{
		int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK)
		{
			closesocket(sock);
			return;
		}
	}

	g_clients[ci].client_socket    = sock;
	g_clients[ci].id               = ci;
	g_clients[ci].playerId         = 0;
	g_clients[ci].x                = 0.0f;
	g_clients[ci].z                = 88.0f;
	g_clients[ci].roomIdx          = UINT32_MAX;
	g_clients[ci].state            = ClientState::Connecting;
	g_clients[ci].connected        = false;
	g_clients[ci].curr_packet_size = 0;
	g_clients[ci].prev_packet_data = 0;

	ZeroMemory(&g_clients[ci].recv_over, sizeof(OverlappedEx));
	g_clients[ci].recv_over.event_type       = OP_RECV;
	g_clients[ci].recv_over.wsabuf.buf       = reinterpret_cast<CHAR*>(g_clients[ci].recv_over.IOCP_buf);
	g_clients[ci].recv_over.wsabuf.len       = sizeof(g_clients[ci].recv_over.IOCP_buf);

	CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), g_hiocp, static_cast<ULONG_PTR>(ci), 0);

	// Immediately send CS_Login
	CS_Login l_packet{};
	l_packet.size = CS_LOGIN_PACKET_SIZE;
	l_packet.id   = static_cast<uint16_t>(PACKET_ID::CS_LOGIN_REQUEST);
	sprintf_s(l_packet.userID, "dummy_%d", ci);
	SendPacket(ci, &l_packet);

	DWORD recv_flag = 0;
	int ret = WSARecv(sock, &g_clients[ci].recv_over.wsabuf, 1,
		nullptr, &recv_flag, &g_clients[ci].recv_over.over, nullptr);
	if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
	{
		closesocket(sock);
		return;
	}

	num_connections++;
}

// -------------------------------------------------------
// Test thread: ramp up clients, drive move sends
// -------------------------------------------------------
void Test_Thread()
{
	while (true)
	{
		// Add new clients up to g_maxTest
		if (active_clients.load() < g_maxTest && num_connections.load() < MAX_CLIENTS)
		{
			auto now     = high_resolution_clock::now();
			auto elapsed = duration_cast<milliseconds>(now - g_lastConnectTime).count();
			if (elapsed >= CONNECT_DELAY_MS)
			{
				g_lastConnectTime = now;
				ConnectNewClient();
			}
		}

		// Send periodic move packets for InRoom clients
		auto now = high_resolution_clock::now();
		int  nc  = num_connections.load();
		for (int i = 0; i < nc; ++i)
		{
			if (!g_clients[i].connected)               continue;
			if (g_clients[i].state != ClientState::InRoom) continue;
			if (g_clients[i].last_move_time + 1s > now)    continue;

			g_clients[i].last_move_time = now;

			CS_Move move_pkt{};
			move_pkt.size      = CS_MOVE_PACKET_SIZE;
			move_pkt.id        = static_cast<uint16_t>(PACKET_ID::CS_MOVE);
			move_pkt.inputFlag = static_cast<uint16_t>(1u << (rand() % 4));
			move_pkt.yaw       = static_cast<float>(rand() % 360);
			SendPacket(i, &move_pkt);
		}
		Sleep(1);
	}
}

// -------------------------------------------------------
// Exported interface (called by DrawModule)
// -------------------------------------------------------
export void SetServerIP(const char* ip)
{
	strncpy_s(g_serverIP, ip, sizeof(g_serverIP) - 1);
	g_serverIP[sizeof(g_serverIP) - 1] = '\0';
}

export void SetMaxClients(int n)
{
	g_maxTest = (n > 0 && n <= MAX_CLIENTS) ? n : 200;
}

export void InitializeNetwork()
{
	for (auto& cl : g_clients)
	{
		cl.connected = false;
		cl.id        = INVALID_CID;
	}
	num_connections   = 0;
	active_clients    = 0;
	g_lastConnectTime = high_resolution_clock::now();

	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

	for (int i = 0; i < 4; ++i)
		g_workerThreads.push_back(new thread{ Worker_Thread });

	g_testThread = thread{ Test_Thread };
}

export void ShutdownNetwork()
{
	g_testThread.join();
	for (auto* pth : g_workerThreads)
	{
		pth->join();
		delete pth;
	}
}

export void Do_Network() {}

export void GetPointCloud(int* size, float** points)
{
	int  index = 0;
	int  nc    = num_connections.load();
	for (int i = 0; i < nc && index < g_maxTest; ++i)
	{
		if (g_clients[i].connected && g_clients[i].state == ClientState::InRoom)
		{
			g_pointCloud[index * 2]     = g_clients[i].x;
			g_pointCloud[index * 2 + 1] = g_clients[i].z;
			index++;
		}
	}
	*size   = index;
	*points = g_pointCloud;
}

export void GetRoomViews(int* numRooms, RoomView** rooms)
{
	int nc = num_connections.load();
	int ri = 0;

	// Clients are grouped: ci 0-3 -> room 0, 4-7 -> room 1, ...
	for (int base = 0; base < nc && ri < MAX_ROOM_VIEWS; base += ROOM_SIZE)
	{
		RoomView rv{};
		rv.roomId = base / ROOM_SIZE;

		for (int j = 0; j < ROOM_SIZE && (base + j) < nc; ++j)
		{
			int ci = base + j;
			if (g_clients[ci].connected && g_clients[ci].state == ClientState::InRoom)
			{
				rv.x[rv.count] = g_clients[ci].x;
				rv.z[rv.count] = g_clients[ci].z;
				rv.count++;
			}
		}

		if (rv.count > 0)
			g_roomViewBuf[ri++] = rv;
	}

	*numRooms = ri;
	*rooms    = g_roomViewBuf;
}
