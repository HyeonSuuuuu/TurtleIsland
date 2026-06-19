#pragma once
#include "pch.h"
#include "Define.h"
#include "GameObject.h"

enum class SessionState : uint8_t
{
	None      = 0,
	Retiring  = 1,
	Connected = 2,
};

// ======================== Session ===========================
// 순수 네트워크 세션: accept/recv/send + 1바이트 길이헤더 패킷 재조립
// 게임 로직은 일절 모른다(콘텐츠는 User : Session 에서)
class Session : public GameObject
{
public:
	static constexpr uint32_t PACKET_DATA_BUFFER_SIZE = 8192;
	static std::atomic<uint32_t> s_nextId;

	Session() = default;
	virtual ~Session();

	virtual void Init(uint32_t id) override;
	virtual void Clear() override;

	constexpr uint32_t GetSlotIndex() const { return m_slotIndex; }

	std::span<const uint8_t> GetRecvData(int32_t size) const;
	void PushPacket(const std::span<const uint8_t> dataSpan);
	std::optional<Packet> PopPacket();

	virtual bool RegisterSocket(HANDLE iocpHandle, SOCKET socket, uint32_t slotIndex);
	virtual void Close(bool isForce = false);

	bool BindAccept(SOCKET listenSock, OverlappedEx& acceptOver, uint32_t slotIndex);
	bool AcceptCompleted(HANDLE iocpHandle, uint32_t slotIndex);
	bool BindRecv();
	bool Send(const std::span<const uint8_t> msg);
	void SendCompleted(uint32_t bytesSent);

	// 콘텐츠 디스패치 진입점(기본 no-op) User 가 override 해 process_packet 역할 수행
	virtual void Dispatch(const Packet& packet) {}

private:
	bool RegisterIOCP(HANDLE iocpHandle, uint32_t slotIndex) const;
	void BindSend();

	SOCKET                    m_socket     = INVALID_SOCKET;
	uint32_t                  m_slotIndex  = INVALID_SESSION_INDEX;
	std::atomic<SessionState> m_sessionState{ SessionState::None };

	OverlappedEx m_recvOverlappedEx{};

	uint8_t* m_packetDataBuffer   = nullptr;
	uint32_t m_packetDataWritePos = 0;
	uint32_t m_packetDataReadPos  = 0;

	std::queue<OverlappedEx*> m_sendQueue;
	std::mutex                m_sendMutex;
	std::atomic<bool>         m_sendRegistered{ false };
};
