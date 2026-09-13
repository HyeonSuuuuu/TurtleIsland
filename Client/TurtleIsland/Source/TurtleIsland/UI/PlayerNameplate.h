#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

namespace PlayerNameplate
{
	inline constexpr float NameWidth = 180.f;
	inline constexpr float BarWidth = 136.f;
	inline constexpr float BarHeight = 16.f;

	struct FTextLayout
	{
		FString Text;
		FSlateFontInfo Font;
		FVector2D Size;
	};

	FTextLayout FitText(FString Text, float MaxWidth, int32 FontSize, int32 MinFontSize);
}
