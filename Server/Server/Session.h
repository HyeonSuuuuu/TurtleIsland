#pragma once
#include "pch.h"
#include "Define.h"
#include "GameObject.h"
#include "../../Common/Protocol.h"

enum class SessionState : uint8_t
{
	None      = 0,
	Retiring  = 1,
	Connected = 2,
};

class Session : public GameObject
{
public:
	static constexpr uint32_t PACKET_DATA_BUFFER_SIZE = 8096;
	static std::atomic<uint32_t> s_nextId;

	Session() = default;
	virtual ~Session();

	virtual void Init(uint32_t id) override;
	virtual void Clear() override;

	constexpr uint64_t GetSocketId() const { return static_cast<uint64_t>(m_socket); }

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

	virtual void Dispatch(const Packet& packet) {}

private:
	bool RegisterIOCP(HANDLE iocpHandle, uint32_t slotIndex) const;
	void BindSend();

	SOCKET                    m_socket             = INVALID_SOCKET;
	std::atomic<SessionState> m_sessionState{ SessionState::None };

	OverlappedEx m_recvOverlappedEx{};

	uint8_t* m_packetDataBuffer   = nullptr;
	uint32_t m_packetDataWritePos = 0;
	uint32_t m_packetDataReadPos  = 0;

	std::queue<OverlappedEx*> m_sendQueue;
	std::mutex                m_sendMutex;
	std::atomic<bool>         m_sendRegistered{ false };
};
