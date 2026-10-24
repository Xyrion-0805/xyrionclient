#include <game/client/QmUi/cards/QmConsoleSettingsLayout.h>

#include <gtest/gtest.h>

TEST(QmConsoleSettingsLayout, CustomColorsConsumeNineSeparatedPublicRows)
{
	for(float Width : {80.0f, 180.0f, 400.0f, 900.0f})
	{
		SCOPED_TRACE(Width);
		const auto Metrics = ResolveSettingsContentMetrics(Width);
		CUIRect Remaining{0, 0, Width, 2000};
		float PreviousBottom = -1.0f;
		for(int i = 0; i < 9; ++i)
		{
			const auto Row = ResolveSettingsColorRowLayout(Remaining, Metrics, false);
			if(i > 0)
				EXPECT_NEAR(Row.m_RowRect.y - PreviousBottom, Metrics.m_LineSpacing, 0.0001f);
			PreviousBottom = Row.m_RowRect.y + Row.m_RowRect.h;
			Remaining.HSplitTop(Row.m_ConsumedHeight, nullptr, &Remaining);
		}
		const float ExtraHeight = QmConsoleSettingsLayout::ContentHeight(Metrics, true, 16) - QmConsoleSettingsLayout::ContentHeight(Metrics, false, 16);
		EXPECT_NEAR(ExtraHeight, Remaining.y, 0.001f);
		EXPECT_GT(ExtraHeight, 9.0f * Metrics.m_ButtonHeight);
	}
}

TEST(QmConsoleSettingsLayout, PublicSpacingChangeAddsSpacingForEveryCustomColorRow)
{
	auto Metrics = ResolveSettingsContentMetrics(180.0f);
	const float Preset = QmConsoleSettingsLayout::ContentHeight(Metrics, false, 16);
	const float Custom = QmConsoleSettingsLayout::ContentHeight(Metrics, true, 16);
	Metrics.m_LineSpacing += 3.0f;
	const float PresetDelta = QmConsoleSettingsLayout::ContentHeight(Metrics, false, 16) - Preset;
	const float CustomDelta = QmConsoleSettingsLayout::ContentHeight(Metrics, true, 16) - Custom;
	EXPECT_NEAR(CustomDelta - PresetDelta, 27.0f, 0.001f);
}

TEST(QmConsoleSettingsLayout, LargerPreviewFontReservesSameAdditionalHeightForBothSchemes)
{
	const auto Metrics = ResolveSettingsContentMetrics(120.0f);
	const float Expected = QmConsoleSettingsLayout::PreviewHeight(32) - QmConsoleSettingsLayout::PreviewHeight(12);
	for(bool Custom : {false, true})
		EXPECT_NEAR(QmConsoleSettingsLayout::ContentHeight(Metrics, Custom, 32) - QmConsoleSettingsLayout::ContentHeight(Metrics, Custom, 12), Expected, 0.001f);
}
