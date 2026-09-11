#pragma once

#include "CoreMinimal.h"

#include <Protocol.h>

struct FDecodedRoomInfo
{
	int32 RoomId = 0;
	FString Title;
	FString HostName;
	int32 UserCount = 0;
};

class TURTLEISLAND_API FClientPacketCodec
{
public:
	static bool HasExactSize(const TArray<uint8>& Packet, uint16 ExpectedSize);
	static bool HasAtLeastSize(const TArray<uint8>& Packet, uint16 MinSize);
	static bool DecodeLobbyRooms(const TArray<uint8>& Packet, TArray<FDecodedRoomInfo>& OutRooms);

	static FString DecodeFixedUtf8(const char* Source, int32 BufferSize);
	static FString DecodeUtf8Payload(const TArray<uint8>& Packet, int32 PayloadOffset);

	static void CopyStringToBuffer(char* Dest, int32 DestSize, const FString& Source);
	static int32 AppendUtf8PayloadClamped(TArray<uint8>& Buffer, int32 PayloadOffset, const FString& Source);

private:
	static int32 FindNullTerminator(const char* Source, int32 BufferSize);
	static int32 ClampUtf8LengthToCodePointBoundary(const ANSICHAR* Data, int32 Length);
};
