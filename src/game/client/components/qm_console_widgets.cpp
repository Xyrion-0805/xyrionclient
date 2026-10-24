#include "menus.h"
#include <game/client/gameclient.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>
#include <engine/client.h>

namespace
{
constexpr float MENU_TAB_HOVER_DURATION = ui_token::motion::HOVER_FADE.m_DurationSec;
constexpr float MENU_TAB_DEFAULT_X_OFFSET = 0.0f;
constexpr float MENU_TAB_DEFAULT_Y_OFFSET = 0.0f;
constexpr float MENU_TAB_DEFAULT_W_OFFSET = 0.0f;
constexpr float MENU_TAB_DEFAULT_H_OFFSET = 0.0f;
constexpr float MENU_TAB_ANIM_EPSILON = 0.0001f;
ColorRGBA MenuTabDefaultColor()
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
		return Base.WithAlpha(std::clamp(g_Config.m_QmUiOpacity / 100.0f, 0.0f, 1.0f));
	}
ColorRGBA MenuTabActiveColor()
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
		return Base.WithAlpha(std::clamp(g_Config.m_QmUiOpacity / 100.0f, 0.0f, 1.0f));
	}
ColorRGBA MenuCapsuleSurfaceColor()
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
		return Base.WithAlpha(std::clamp(g_Config.m_QmUiOpacity / 100.0f, 0.0f, 1.0f));
	}
ColorRGBA MenuCapsuleTabActiveLabelColor()
	{
		return ui_widget::CapsuleTabBarActiveLabelColor(MenuCapsuleSurfaceColor());
	}
ColorRGBA MenuCapsuleTabInactiveLabelColor()
	{
		return ui_widget::CapsuleTabBarInactiveLabelColor(MenuCapsuleSurfaceColor());
	}
ColorRGBA MenuCapsuleTabHoverColor()
	{
		return ui_widget::CapsuleTabBarHoverColor(MenuCapsuleSurfaceColor());
	}
}
IUiContext CMenus::TabBarUiContext() const
{
	IUiContext Context;
	Context.m_pUi = Ui();
	Context.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	return Context;
}
ui_widget::SCapsuleTabBarStyle CMenus::SettingsCapsuleTabBarStyle() const
{
	return CapsuleTabBarStyleFor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor)).WithAlpha(g_Config.m_QmUiOpacity / 100.0f));
}
ui_widget::SCapsuleTabBarStyle CMenus::CapsuleTabBarStyleFor(const ColorRGBA &SurfaceColor) const
{
	ui_widget::SCapsuleTabBarStyle Style;
	// 胶囊只当滑块轨道：在容器表面上再压一层暗色，深浅主题下都比容器深一档。
	Style.m_CapsuleColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.16f);
	Style.m_IndicatorColor = ui_widget::CapsuleTabBarIndicatorColor(SurfaceColor);
	Style.m_ActiveLabelColor = ui_widget::CapsuleTabBarActiveLabelColor(SurfaceColor);
	Style.m_InactiveLabelColor = ui_widget::CapsuleTabBarInactiveLabelColor(SurfaceColor);
	return Style;
}
float CMenus::ResolveMenuTabAnimationValue(const void *pButtonId, const bool Active, const float DurationSec) const
{
	const float Target = Active ? 1.0f : 0.0f;
	if(Ui()->RenderOnly())
		return Target;

	static const uint64_t s_ScopeHash = static_cast<uint64_t>(str_quickhash("menu_tab_hover"));
	const uint64_t NodeKey = BuildUiAnimNodeKey(s_ScopeHash, reinterpret_cast<uint64_t>(pButtonId));
	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	return std::clamp(ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::SCALE, Target, DurationSec, EEasing::EASE_OUT), 0.0f, 1.0f);
}
IUiContext CMenus::SettingsUiContext(const char *pScope, const float UiScale)
{
	m_SettingsUiTheme = ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(g_Config.m_QmUiFocusColor), ColorHSLA(g_Config.m_QmUiAccentColor).WithAlpha(g_Config.m_QmUiAccentOpacity / 100.0f), ColorHSLA(g_Config.m_QmUiSelectedColor));
	IUiContext Context;
	Context.m_pUi = Ui();
	Context.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	Context.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	Context.m_pMenus = this;
	Context.m_pTooltips = &GameClient()->m_Tooltips;
	Context.m_pTextRender = TextRender();
	Context.m_pTheme = &m_SettingsUiTheme;
	Context.m_ScopeHash = MakeUiScopeHash(pScope);
	Context.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	Context.m_UiScale = UiScale;
	if(Ui()->RenderOnly())
	{
		Context.m_pAnim = nullptr;
		Context.m_pTree = nullptr;
	}
	return Context;
}
int CMenus::DoButton_MenuTab(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator, const ColorRGBA *pDefaultColor, const ColorRGBA *pActiveColor, const ColorRGBA *pHoverColor, float EdgeRounding, const CCommunityIcon *pCommunityIcon, CUIElement *pTextUiElement, float FontSize, bool CapsuleTab)
{
	return DoButton_MenuTabInternal(pButtonContainer, pText, EQmIcon::COUNT, nullptr, Checked, pRect, Corners, pAnimator, pDefaultColor, pActiveColor, pHoverColor, EdgeRounding, pCommunityIcon, pTextUiElement, FontSize, CapsuleTab);
}
int CMenus::DoButton_MenuTabInternal(CButtonContainer *pButtonContainer, const char *pText, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator, const ColorRGBA *pDefaultColor, const ColorRGBA *pActiveColor, const ColorRGBA *pHoverColor, float EdgeRounding, const CCommunityIcon *pCommunityIcon, CUIElement *pTextUiElement, float FontSize, bool CapsuleTab)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	const bool MouseInside = Ui()->HotItem() == pButtonContainer;
	CUIRect AnimatedLabelRect = *pRect;
	const bool TabActive = MouseInside;
	const float AnimValue = ResolveMenuTabAnimationValue(pButtonContainer, TabActive, MENU_TAB_HOVER_DURATION);

	float XOffset = MENU_TAB_DEFAULT_X_OFFSET;
	float YOffset = MENU_TAB_DEFAULT_Y_OFFSET;
	float WOffset = MENU_TAB_DEFAULT_W_OFFSET;
	float HOffset = MENU_TAB_DEFAULT_H_OFFSET;
	bool RepositionLabel = false;
	bool ScaleLabel = false;
	if(pAnimator != nullptr)
	{
		XOffset = pAnimator->m_XOffset;
		YOffset = pAnimator->m_YOffset;
		WOffset = pAnimator->m_WOffset;
		HOffset = pAnimator->m_HOffset;
		RepositionLabel = pAnimator->m_RepositionLabel;
		ScaleLabel = pAnimator->m_ScaleLabel;
	}
	AnimatedLabelRect.w += AnimValue * WOffset;
	AnimatedLabelRect.h += AnimValue * HOffset;
	AnimatedLabelRect.x += AnimValue * XOffset;
	AnimatedLabelRect.y += AnimValue * YOffset;

	if(CapsuleTab)
	{
		// 胶囊 Tab 的激活外观由滑块胶囊承担，Tab 自己不再画分块底色，只保留 hover 反馈。
		// hover 胶囊与激活滑块同几何（同一内缩），不能比滑块大出一圈。
		if(AnimValue > MENU_TAB_ANIM_EPSILON)
		{
			ColorRGBA HoverColor = MenuCapsuleTabHoverColor();
			HoverColor.a *= AnimValue;
			CUIRect HoverRect;
			pRect->Margin(ui_widget::CAPSULE_TAB_INDICATOR_INSET, &HoverRect);
			DrawRoundedSurface(Ui(), HoverRect, HoverColor, ColorRGBA(), ui_token::radius::PILL, 0.0f, Corners);
		}
	}
	else if(Checked)
	{
		ColorRGBA ColorMenuTab = ms_ColorTabbarActive;
		if(pActiveColor)
			ColorMenuTab = *pActiveColor;
		else
			ColorMenuTab = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.42f);

		DrawRoundedSurface(Ui(), *pRect, ColorMenuTab, ColorRGBA(), EdgeRounding, 0.0f, Corners);
	}
	else
	{
		if(AnimValue > MENU_TAB_ANIM_EPSILON)
		{
			ColorRGBA HoverColorMenuTab = ms_ColorTabbarHover;
			if(pHoverColor)
				HoverColorMenuTab = *pHoverColor;
			else
				HoverColorMenuTab = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.20f);

			const ColorRGBA IdleColor = pDefaultColor != nullptr ? *pDefaultColor : ms_ColorTabbarInactive;
			const ColorRGBA AnimatedColor(
				mix(IdleColor.r, HoverColorMenuTab.r, AnimValue),
				mix(IdleColor.g, HoverColorMenuTab.g, AnimValue),
				mix(IdleColor.b, HoverColorMenuTab.b, AnimValue),
				mix(IdleColor.a, HoverColorMenuTab.a, AnimValue));
			DrawRoundedSurface(Ui(), *pRect, AnimatedColor, ColorRGBA(), EdgeRounding, 0.0f, Corners);
		}
		else
		{
			ColorRGBA ColorMenuTab = ms_ColorTabbarInactive;
			if(pDefaultColor)
				ColorMenuTab = *pDefaultColor;

			DrawRoundedSurface(Ui(), *pRect, ColorMenuTab, ColorRGBA(), EdgeRounding, 0.0f, Corners);
		}
	}

	// 按压反馈复用普通按钮的公共表面策略，覆盖已选中的胶囊滑块。
	if(Ui()->Enabled() && !Ui()->RenderOnly() && Ui()->MouseHovered(pRect) && Ui()->CheckActiveItem(pButtonContainer) && Ui()->MouseButton(0))
	{
		CUIRect FeedbackRect = *pRect;
		if(CapsuleTab)
			pRect->Margin(ui_widget::CAPSULE_TAB_INDICATOR_INSET, &FeedbackRect);
		const ColorRGBA Surface = CapsuleTab && Checked ? MenuTabActiveColor() : MenuTabDefaultColor();
		DrawRoundedSurface(Ui(), FeedbackRect, ResolveUiIconButtonFeedback(CompositeUiSurface(Surface), true, true, true), ColorRGBA(), CapsuleTab ? ui_token::radius::PILL : EdgeRounding, 0.0f, Corners);
	}

	if(pAnimator != nullptr)
	{
		if(RepositionLabel)
		{
			AnimatedLabelRect.x += AnimatedLabelRect.w - pRect->w + AnimatedLabelRect.x - pRect->x;
			AnimatedLabelRect.y += AnimatedLabelRect.h - pRect->h + AnimatedLabelRect.y - pRect->y;
		}

		if(!ScaleLabel)
		{
			AnimatedLabelRect.w = pRect->w;
			AnimatedLabelRect.h = pRect->h;
		}
	}

	// Keep tab contents clipped horizontally to the original button rect.
	// Extend clipping vertically to include the animated rect so "float up"
	// animations don't cut off icon/text.
	CUIRect ContentClip = *pRect;
	const float ClipTop = std::min(pRect->y, AnimatedLabelRect.y);
	const float ClipBottom = std::max(pRect->y + pRect->h, AnimatedLabelRect.y + AnimatedLabelRect.h);
	ContentClip.y = ClipTop;
	ContentClip.h = ClipBottom - ClipTop;
	Ui()->ClipEnable(&ContentClip);

	const ColorRGBA PreviousLabelColor = TextRender()->GetTextColor();
	if(CapsuleTab)
		TextRender()->TextColor(Checked ? MenuCapsuleTabActiveLabelColor() : MenuCapsuleTabInactiveLabelColor());

	if(pCommunityIcon)
	{
		CUIRect CommunityIcon;
		CUIRect StaticIconRect = *pRect;
		StaticIconRect.Margin(2.0f, &CommunityIcon);
		m_CommunityIcons.Render(pCommunityIcon, CommunityIcon, true);
	}
	else
	{
		CUIRect Label;
		AnimatedLabelRect.HMargin(2.0f, &Label);
		const bool FixedFontSize = FontSize > 0.0f;
		FontSize = FixedFontSize ? FontSize : Label.h * CUi::ms_FontmodHeight;
		SLabelProperties Props;
		if(FixedFontSize)
		{
			Props.m_MaxWidth = Label.w;
			Props.m_MinimumFontSize = FontSize;
			Props.m_EllipsisAtEnd = true;
		}
		if(Icon != EQmIcon::COUNT)
			Ui()->DoLabel_QmIcon(&Label, Icon, pFallbackIcon, FontSize, TEXTALIGN_MC, Props);
		else if(pText != nullptr && pTextUiElement != nullptr && pTextUiElement->AreRectsInit())
			Ui()->DoLabelStreamed(*pTextUiElement->Rect(0), &Label, pText, FontSize, TEXTALIGN_MC, Props);
		else if(pText != nullptr)
			Ui()->DoLabel(&Label, pText, FontSize, TEXTALIGN_MC, Props);
	}

	if(CapsuleTab)
		TextRender()->TextColor(PreviousLabelColor);
	Ui()->ClipDisable();

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}
int CMenus::DoSegmentedChoice(CButtonContainer *pButtons, const char *const *ppLabels, const int Count, const int Current, const CUIRect &Segments, const float Rounding, const ui_widget::SCapsuleTabBarStyle *pCapsuleStyle, float FontSize, const void *pConfigValue, const void *pSecondConfigValue)
{
	return DoSegmentedChoiceInternal(pButtons, ppLabels, Count, Current, Segments, Rounding, pCapsuleStyle, FontSize, pConfigValue, pSecondConfigValue, false);
}
int CMenus::DoSegmentedChoiceInternal(CButtonContainer *pButtons, const char *const *ppLabels, const int Count, const int Current, const CUIRect &Segments, const float Rounding, const ui_widget::SCapsuleTabBarStyle *pCapsuleStyle, float FontSize, const void *pConfigValue, const void *pSecondConfigValue, const bool Navigation)
{
	int NewValue = Current;
	if(pButtons == nullptr)
		return NewValue;
	if(FontSize <= 0.0f)
		FontSize = std::max(0.0f, Segments.h - 4.0f) * CUi::ms_FontmodHeight;
	SSettingsRadioState &State = m_SettingsRadioStates[pButtons];
	const auto Layout = PrepareSettingsChoice(*Ui(), State, Segments, ppLabels, Count, FontSize);
	if(Count <= 0 || ppLabels == nullptr)
		return NewValue;
	if(Layout.m_UseDropdown)
	{
		CUIRect Trigger = Segments;
		CUi::SDropDownProperties Props;
		Props.m_FontSize = FontSize;
		Props.m_pConfigValue = pConfigValue;
        Props.m_pSecondConfigValue = pSecondConfigValue;
        return Ui()->DoDropDown(&Trigger, Current >= 0 && Current < Count ? Current : -1, ppLabels, Count, State.m_DropDown, Props);
	}
	const ui_widget::SCapsuleTabBarStyle FallbackStyle = SettingsCapsuleTabBarStyle();
	const ui_widget::SCapsuleTabBarStyle &Style = pCapsuleStyle != nullptr ? *pCapsuleStyle : FallbackStyle;
	const uint64_t SegmentGroup = BuildUiAnimNodeKey(MakeUiScopeHash("segmented_choice_capsule"), reinterpret_cast<uint64_t>(pButtons));
	if(Navigation)
		NewValue = ui_widget::CapsuleTabBar(TabBarUiContext(), SegmentGroup, Layout.m_aSlots.data(), Layout.m_SegmentCount, Current, Style);
	else
		ui_widget::CapsuleTabBarChrome(TabBarUiContext(), SegmentGroup, Layout.m_aSlots.data(), Layout.m_SegmentCount, Current >= 0 && Current < Count ? Current : -1, Style);
	for(int i = 0; i < Layout.m_SegmentCount; ++i)
	{
		Ui()->DoConfigTooltip(&pButtons[i], &Layout.m_aSlots[i], pConfigValue, pSecondConfigValue);
		const bool Clicked = DoButton_MenuTab(&pButtons[i], ppLabels[i], (Navigation ? NewValue : Current) == i, &Layout.m_aSlots[i], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, Rounding, nullptr, nullptr, FontSize, true) != 0;
		if(!Navigation && Clicked)
			NewValue = i;
	}
	return NewValue;
}
bool CMenus::IsSettingsPageActive() const
{
	if(!IsActive())
		return false;
	if(Client()->State() == IClient::STATE_ONLINE)
		return m_GamePage == PAGE_SETTINGS;
	return m_MenuPage == PAGE_SETTINGS;
}
int CMenus::DoButton_CheckBox_Common_WithLabelElement(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, const unsigned Flags, CUIElement *pLabelElement, const bool ProcessInput, const float LabelFontSize, const bool MarkedControl)
{
	Ui()->DoConfigTooltip(pId, pRect, pId);
	const bool HasLabel = pText != nullptr && pText[0] != '\0';
	const bool HasCustomGlyph = MarkedControl || (pBoxText[0] != '\0' && pBoxText[0] != 'X');
	const IUiContext Context = ui_widget::ControlContext(Ui());
	CUIRect Label, Control;
	if(HasCustomGlyph)
	{
		pRect->VSplitLeft(std::min(pRect->h, pRect->w), &Control, &Label);
		Control.Margin(std::min(2.0f, std::min(Control.w, Control.h) * 0.25f), &Control);
		Label.VSplitLeft(std::min(5.0f, Label.w), nullptr, &Label);
		ui_widget::DrawMarkedControl(Context, pId, pBoxText, Control, pRect);
	}
	else
	{
		const auto Layout = ui_widget::ResolveBooleanControlLayout(*pRect, HasLabel);
		Label = Layout.m_LabelRect;
		Control = Layout.m_ControlRect;
		ui_widget::DrawToggle(Context, pId, pBoxText[0] == 'X', Control, true, ProcessInput, pRect);
	}
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pId, pRect, pId, &Label);
	if(HasLabel)
	{
		const float FontSize = LabelFontSize > 0.0f ? LabelFontSize : std::min(ui_token::font::BODY, pRect->h * CUi::ms_FontmodHeight);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_MinimumFontSize = FontSize;
		Props.m_EllipsisAtEnd = true;
		if(pLabelElement != nullptr)
			Ui()->DoLabelStreamed(*pLabelElement->Rect(0), &Label, pText, FontSize, TEXTALIGN_ML, Props);
		else
			Ui()->DoLabel(&Label, pText, FontSize, TEXTALIGN_ML, Props);
	}
	return ProcessInput ? Ui()->DoButtonLogic(pId, 0, pRect, Flags) : 0;
}
int CMenus::DoButton_CheckBox(const void *pId, const char *pText, int Checked, const CUIRect *pRect, const float BodySize)
{
	return DoButton_CheckBox_Common_WithLabelElement(pId, pText, Checked ? "X" : "", pRect, BUTTONFLAG_LEFT, nullptr, true, BodySize);
}
ColorHSLA CMenus::DoLine_ColorPicker(CButtonContainer *pResetId, const SSettingsContentMetrics &Metrics, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, const ColorRGBA DefaultColor, bool CheckBoxSpacing, int *pCheckBoxValue, bool Alpha, bool TrailingSpacing)
{
	CUIRect HintRect = *pMainRect;
	const SSettingsColorRowLayout Layout = ResolveSettingsColorRowLayout(*pMainRect, Metrics, CheckBoxSpacing && pCheckBoxValue == nullptr, TrailingSpacing);
	HintRect.h = Layout.m_ConsumedHeight;
	pMainRect->y += Layout.m_ConsumedHeight;
	pMainRect->h = std::max(0.0f, pMainRect->h - Layout.m_ConsumedHeight);
	CUIRect Label = Layout.m_LabelRect;

	if(pCheckBoxValue != nullptr)
	{
		if(DoButton_CheckBox(pCheckBoxValue, pText, *pCheckBoxValue, &Label, Metrics.m_BodySize))
			*pCheckBoxValue ^= 1;
	}
	if(pCheckBoxValue == nullptr)
	{
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_DisallowNewline = true;
		Props.m_StopAtEnd = true;
		Props.m_MinimumFontSize = 6.0f;
		Ui()->DoLabel(&Label, pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
	}

	if(!Ui()->RenderOnly())
	{
		CUIRect HoverRect = Label;
		HoverRect.w = Layout.m_ColorButtonRect.x + Layout.m_ColorButtonRect.w - Label.x;
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pColorValue, &HoverRect, pColorValue, &Label);
	}
	const ColorHSLA PickedColor = DoButton_ColorPicker(&Layout.m_ColorButtonRect, pColorValue, Alpha);

	if(Ui()->DoButton_PopupMenu(pResetId, Localize("Reset"), &Layout.m_ResetButtonRect, Metrics.m_BodySize, TEXTALIGN_MC))
	{
		*pColorValue = color_cast<ColorHSLA>(DefaultColor).Pack(Alpha);
	}

	Ui()->DoConfigTooltip(pColorValue, &HintRect, pColorValue);
	return PickedColor;
}
