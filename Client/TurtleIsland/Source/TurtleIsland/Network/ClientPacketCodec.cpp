#include "Network/ClientPacketCodec.h"

bool FClientPacketCodec::HasExactSize(const TArray<uint8>& Packet, uint16 ExpectedSize)
{
	if (Packet.Num() != static_cast<int32>(ExpectedSize))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Codec] Packet size mismatch. expected=%u actual=%d"), ExpectedSize, Packet.Num());
		return false;
	}
	return true;
}

bool FClientPacketCodec::HasAtLeastSize(const TArray<uint8>& Packet, uint16 MinSize)
{
	if (Packet.Num() < static_cast<int32>(MinSize))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Codec] Packet too short. minimum=%u actual=%d"), MinSize, Packet.Num());
		return false;
	}
	return true;
}

bool FClientPacketCodec::DecodeLobbyRooms(const TArray<uint8>& Packet, TArray<FDecodedRoomInfo>& OutRooms)
{
	OutRooms.Reset();

	if (!HasAtLeastSize(Packet, static_cast<uint16>(sizeof(PacketHeader) + sizeof(int16))))
	{
		return false;
	}

	const int32 HeaderBytes = sizeof(PacketHeader);
	const int16 RoomCount = *reinterpret_cast<const int16*>(Packet.GetData() + HeaderBytes);
	if (RoomCount < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Codec] Negative room count: %d"), RoomCount);
		return false;
	}

	const int32 PayloadBytes = Packet.Num() - HeaderBytes - sizeof(int16);
	if (PayloadBytes < static_cast<int32>(RoomCount) * static_cast<int32>(sizeof(RoomInfo)))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Codec] Lobby packet truncated. count=%d payload=%d"), RoomCount, PayloadBytes);
		return false;
	}

	const uint8* RoomPtr = Packet.GetData() + HeaderBytes + sizeof(int16);
	for (int16 i = 0; i < RoomCount; ++i)
	{
		const RoomInfo* Info = reinterpret_cast<const RoomInfo*>(RoomPtr);

		FDecodedRoomInfo Data;
		Data.RoomId = Info->roomId;
		Data.Title = DecodeFixedUtf8(Info->roomTitle, MAX_ROOM_TITLE_LEN);
		Data.HostName = DecodeFixedUtf8(Info->hostName, MAX_USER_ID_LEN);
		Data.UserCount = Info->userCount;
		OutRooms.Add(MoveTemp(Data));

		RoomPtr += sizeof(RoomInfo);
	}

	return true;
}

FString FClientPacketCodec::DecodeFixedUtf8(const char* Source, int32 BufferSize)
{
	if (!Source || BufferSize <= 0)
	{
		return FString();
	}

	const int32 TextLen = FindNullTerminator(Source, BufferSize);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Source), TextLen);
	Bytes.Add(0);
	return UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()));
}

FString FClientPacketCodec::DecodeUtf8Payload(const TArray<uint8>& Packet, int32 PayloadOffset)
{
	if (PayloadOffset < 0 || Packet.Num() <= PayloadOffset)
	{
		return FString();
	}

	TArray<uint8> Bytes;
	Bytes.Append(Packet.GetData() + PayloadOffset, Packet.Num() - PayloadOffset);
	Bytes.Add(0);
	return UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()));
}

void FClientPacketCodec::CopyStringToBuffer(char* Dest, int32 DestSize, const FString& Source)
{
	if (!Dest || DestSize <= 0)
	{
		return;
	}

	FMemory::Memzero(Dest, DestSize);
	FTCHARToUTF8 Converter(*Source);
	const int32 CopySize = ClampUtf8LengthToCodePointBoundary(
		reinterpret_cast<const ANSICHAR*>(Converter.Get()),
		FMath::Min(Converter.Length(), DestSize - 1));
	if (CopySize > 0)
	{
		FMemory::Memcpy(Dest, Converter.Get(), CopySize);
	}
}

int32 FClientPacketCodec::AppendUtf8PayloadClamped(TArray<uint8>& Buffer, int32 PayloadOffset, const FString& Source)
{
	if (PayloadOffset < 0 || Buffer.Num() < PayloadOffset)
	{
		return 0;
	}

	FTCHARToUTF8 Converter(*Source);
	const int32 MaxPayloadBytes = Buffer.Num() - PayloadOffset;
	const int32 CopySize = ClampUtf8LengthToCodePointBoundary(
		reinterpret_cast<const ANSICHAR*>(Converter.Get()),
		FMath::Min(Converter.Length(), MaxPayloadBytes));

	if (CopySize > 0)
	{
		FMemory::Memcpy(Buffer.GetData() + PayloadOffset, Converter.Get(), CopySize);
	}
	return CopySize;
}

int32 FClientPacketCodec::FindNullTerminator(const char* Source, int32 BufferSize)
{
	for (int32 i = 0; i < BufferSize; ++i)
	{
		if (Source[i] == '\0')
		{
			return i;
		}
	}
	return BufferSize;
}

int32 FClientPacketCodec::ClampUtf8LengthToCodePointBoundary(const ANSICHAR* Data, int32 Length)
{
	if (!Data || Length <= 0)
	{
		return 0;
	}

	int32 Index = 0;
	int32 LastValid = 0;
	while (Index < Length)
	{
		const uint8 Lead = static_cast<uint8>(Data[Index]);
		int32 CharBytes = 1;
		if ((Lead & 0x80) == 0)
		{
			CharBytes = 1;
		}
		else if ((Lead & 0xE0) == 0xC0)
		{
			CharBytes = 2;
		}
		else if ((Lead & 0xF0) == 0xE0)
		{
			CharBytes = 3;
		}
		else if ((Lead & 0xF8) == 0xF0)
		{
			CharBytes = 4;
		}
		else
		{
			break;
		}

		if (Index + CharBytes > Length)
		{
			break;
		}

		for (int32 Trail = 1; Trail < CharBytes; ++Trail)
		{
			if ((static_cast<uint8>(Data[Index + Trail]) & 0xC0) != 0x80)
			{
				return LastValid;
			}
		}

		Index += CharBytes;
		LastValid = Index;
	}

	return LastValid;
}
