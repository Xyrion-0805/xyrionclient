/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIFORMLOGIC_H
#define GAME_CLIENT_QMUI_UIFORMLOGIC_H

#include <game/client/QmUi/UiForms.h>
#include <game/client/ui_rect.h>

#include <string>

namespace ui_widget
{
	struct SNumericValueFormat
	{
		int m_DisplayDivisor = 1;
		int m_Precision = 0;
		bool m_AllowInfinite = false;
		int m_InfiniteStoredValue = 0;
	};

	enum class ENumericLayoutFeedback
	{
		NONE,
		TWO_ROWS,
		TRACK_AT_MINIMUM,
	};

	struct SNumericFieldLayout
	{
		CUIRect m_LabelRect;
		CUIRect m_SliderRect;
		CUIRect m_InputRect;
		CUIRect m_UnitRect;
		bool m_TwoRows = false;
		ENumericLayoutFeedback m_Feedback = ENumericLayoutFeedback::NONE;
	};

	std::string FormatNumericFieldValue(int StoredValue, const SNumericValueFormat &Format);
	bool ParseNumericFieldValue(const char *pText, const SNumericValueFormat &Format, int StoredMin, int StoredMax, int *pStoredValue);
	bool NumericFieldShouldCommit(EInputCommitPolicy Policy, bool SliderReleased, const SInputFieldResult &InputResult);
	bool UpdateNumericFieldSliderCommit(SNumericFieldCommitState &State, EInputCommitPolicy Policy, bool SliderActive, bool SliderReleased, int CandidateStoredValue, int *pStoredValue);
	SNumericFieldLayout ResolveNumericFieldLayout(const CUIRect &Rect, bool HasLabel, bool HasUnit, float UiScale = 1.0f);
} // namespace ui_widget

#endif // GAME_CLIENT_QMUI_UIFORMLOGIC_H
