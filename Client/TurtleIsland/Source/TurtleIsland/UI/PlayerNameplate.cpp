#include "UI/PlayerNameplate.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/BreakIterator.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

PlayerNameplate::FTextLayout PlayerNameplate::FitText(FString Text, float MaxWidth, int32 FontSize, int32 MinFontSize)
{
	for (TCHAR& Character : Text)
		if (FChar::IsWhitespace(Character) || FChar::IsControl(Character)) Character = TEXT(' ');
	FTextLayout Layout{Text.TrimStartAndEnd(), FCoreStyle::GetDefaultFontStyle("Bold", FontSize), FVector2D::ZeroVector};
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	Layout.Size = Measure->Measure(Layout.Text, Layout.Font);
	while (Layout.Size.X > MaxWidth && Layout.Font.Size > MinFontSize)
	{
		Layout.Font.Size -= 1;
		Layout.Size = Measure->Measure(Layout.Text, Layout.Font);
	}
	if (Layout.Size.X > MaxWidth)
	{
		// Truncate at grapheme boundaries so UTF-16 pairs and combining marks stay intact.
		const TSharedRef<IBreakIterator> Boundaries = FBreakIterator::CreateCharacterBoundaryIterator();
		Boundaries->SetString(Layout.Text);
		Boundaries->ResetToEnd();
		const FString FullText = Layout.Text;
		for (int32 End = Boundaries->MoveToPrevious(); End != INDEX_NONE; End = Boundaries->MoveToPrevious())
		{
			Layout.Text = FullText.Left(End) + TEXT("...");
			Layout.Size = Measure->Measure(Layout.Text, Layout.Font);
			if (Layout.Size.X <= MaxWidth) break;
		}
	}
	return Layout;
}
