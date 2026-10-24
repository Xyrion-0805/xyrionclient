#ifndef GAME_CLIENT_QMUI_SETTINGSCHOICE_H
#define GAME_CLIENT_QMUI_SETTINGSCHOICE_H

#include "SettingsPageLayout.h"

#include <engine/textrender.h>

#include <game/client/ui.h>

struct SSettingsChoiceState
{
	CUi::SDropDownState m_DropDown;
	CSettingsChoiceWidthCache m_Widths;
};

// 测量、输入预布局和正式绘制都使用这份槽位；形式变化统一释放失效下拉。
inline SSettingsChoiceLayout PrepareSettingsChoice(CUi &Ui, SSettingsChoiceState &State, const CUIRect &Controls, const char *const *ppLabels, int Count, float FontSize)
{
	if(Count <= 0 || ppLabels == nullptr)
		Count = 0;
	const float Width = State.m_Widths.Resolve(ppLabels, Count, FontSize, Ui.TextRender()->GlyphAtlasRevision(), [&Ui](const char *pText, float Size) { return Ui.TextRender()->TextWidth(Size, pText); });
	const auto Layout = ResolveSettingsChoiceLayout(Controls, Count, Width + 2.0f * ui_token::spacing::MD);
	if((!Layout.m_UseDropdown || Count == 0) && !Ui.RenderOnly())
	{
		Ui.ClosePopupMenu(&State.m_DropDown.m_SelectionPopupContext, true);
		State.m_DropDown.m_DropDownState.Reset();
		State.m_DropDown.m_SelectionPopupContext.m_SelectionIndex = -1;
	}
	return Layout;
}

#endif
