/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menu_background.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

void CMenus::RenderSettings(CUIRect MainView)
{
	if(m_AssetsEditorState.m_Open && g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
	{
		MainView.Margin(20.0f, &MainView);
		RenderSettingsAssets(MainView);
		return;
	}
	if(g_Config.m_UiSettingsPage < 0 || g_Config.m_UiSettingsPage >= SETTINGS_LENGTH)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	if(g_Config.m_UiSettingsPage == SETTINGS_MODS)
	{
		g_Config.m_UiSettingsPage = SETTINGS_XYRION;
		g_Config.m_XcSettingsTab = 2;
	}
	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	const SSettingsShellLayoutFrame Shell = ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f);
	MainView = Shell.m_ContentRect;
	CUIRect TabBar = Shell.m_TabBarRect, RestartBar = Shell.m_RestartBarRect, Button;
	const IUiContext Context = SettingsUiContext("settings-shell", Shell.m_UiScale);
	const ColorRGBA Surface = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor)).WithAlpha(g_Config.m_QmUiOpacity / 100.0f);
	DrawRoundedSurface(Context, Shell.m_ContentPanelRect, Surface, ColorRGBA(), ui_token::radius::CARD);
	DrawRoundedSurface(Context, TabBar, Surface, ColorRGBA(), ui_token::radius::CARD);
	TabBar.Margin(10.0f, &TabBar);
	TabBar.HSplitTop(38.0f, &Button, &TabBar);
	Ui()->DoLabel(&Button, Localize("Settings"), ui_token::font::HEADLINE_LG, TEXTALIGN_MC);

	const char *apTabs[SETTINGS_LENGTH] = {
		Localize("Language"),
		Localize("General"),
		Localize("Player"),
		Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"),
		Localize("Appearance"),
		Localize("Controls"),
		Localize("Graphics"),
		Localize("Sound"),
		Localize("DDNet"),
		Localize("Assets"),
		TCLocalize("TClient"), // TClient
		"Xyrion",
		TCLocalize("Mod 管理器"),
		Localize("Profiles"), // TClient
		Localize("Configs"), // TClient
		Localize("Credits")};
	static CButtonContainer s_aTabButtons[SETTINGS_LENGTH];
	static CScrollRegion s_NavigationScroll;
	const SQmScrollRequest NavigationScrollRequest{EQmScrollProfile::SETTINGS_INNER};
	CScrollRegionParams NavigationScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(NavigationScrollRequest, 1.0f));
	s_NavigationScroll.Begin(&TabBar, &NavigationScrollParams);
	std::array<CUIRect, SETTINGS_LENGTH> Slots{};
	std::array<int, SETTINGS_LENGTH> Pages{};
	int Count = 0;
	for(int i = 0; i < SETTINGS_LENGTH; ++i)
	{
		if(i == SETTINGS_MODS)
			continue;
		TabBar.HSplitTop(ui_token::settings::TAB_GAP, nullptr, &TabBar);
		TabBar.HSplitTop(ui_token::settings::TAB_HEIGHT, &Slots[Count], &TabBar);
		s_NavigationScroll.AddRect(Slots[Count]);
		Pages[Count++] = i;
	}
	const int Pressed = ui_widget::CapsuleTabBarPressedIndex(TabBarUiContext(), Slots.data(), Count, 0.0f);
	if(Pressed >= 0 && g_Config.m_UiSettingsPage != Pages[Pressed])
	{
		Ui()->ClosePopupMenus();
		Ui()->SetActiveItem(nullptr);
		g_Config.m_UiSettingsPage = Pages[Pressed];
	}
	const ColorRGBA Selected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.42f);
	const ColorRGBA Hover = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.20f);
	const ColorRGBA Idle(0, 0, 0, 0);
	for(int Index = 0; Index < Count; ++Index)
	{
		const int Page = Pages[Index];
		DoButton_MenuTab(&s_aTabButtons[Page], apTabs[Page], g_Config.m_UiSettingsPage == Page, &Slots[Index], IGraphics::CORNER_ALL, nullptr, &Idle, &Selected, &Hover, ui_token::radius::CARD, nullptr, nullptr, 14.0f, false);
	}
	s_NavigationScroll.End();
	const auto RenderPage = [&](CUIRect MainView) {

		if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
			RenderLanguageSettings(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
			RenderSettingsGeneral(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
			RenderSettingsPlayer(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
			if(Client()->IsSixup())
				RenderSettingsTee7(MainView);
			else
				RenderSettingsTee(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
			RenderSettingsAppearance(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
			m_MenusSettingsControls.Render(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
			RenderSettingsGraphics(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
			RenderSettingsSound(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
			RenderSettingsDDNet(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
			RenderSettingsAssets(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT) // TClient
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
			RenderSettingsTClient(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES) // TClient
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
			RenderSettingsTClientProfiles(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_XYRION)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
			RenderSettingsXyrion(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_MODS)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
			g_Config.m_XcSettingsTab = 2;
			g_Config.m_UiSettingsPage = SETTINGS_XYRION;
			RenderSettingsXyrion(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS) // TClient
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
			RenderSettingsTClientConfigs(MainView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CREDITS);
			RenderSettingsCredits(MainView);
		}
		else
		{
			dbg_assert_failed("ui_settings_page invalid");
		}
	};
	if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER || g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE || g_Config.m_UiSettingsPage == SETTINGS_GENERAL || g_Config.m_UiSettingsPage == SETTINGS_SOUND || g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS || g_Config.m_UiSettingsPage == SETTINGS_DDNET || g_Config.m_UiSettingsPage == SETTINGS_TCLIENT || g_Config.m_UiSettingsPage == SETTINGS_XYRION)
		RenderPage(MainView);
	else
	{
		static const char *apIds[SETTINGS_LENGTH] = {"xyrion-page:language", "", "xyrion-page:player", "xyrion-page:tee", "xyrion-page:appearance", "xyrion-page:controls", "xyrion-page:graphics", "", "xyrion-page:ddnet", "xyrion-page:assets", "xyrion-page:tclient", "", "", "xyrion-page:profiles", "xyrion-page:configs", "xyrion-page:credits"};
		const int Page = g_Config.m_UiSettingsPage;
		const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
		const float ChromeHeight = BuildSettingsCardFrame({0, 0, MainView.w, 0}, {}, 0.0f, Metrics.m_UiScale).m_Rect.h;
		const float ContentHeight = std::max(1.0f, MainView.h - ChromeHeight - 2.0f * ui_token::settings::PAGE_INSET * Metrics.m_UiScale);
		std::vector<SSettingsCardDefinition> Cards;
		auto Card = SettingsCardDefinition(apIds[Page], apTabs[Page], ContentHeight, [&](CUIRect &Content) {
			RenderPage(Content);
			Content.y += Content.h;
			Content.h = 0.0f;
		});
		// 旧页面的内层列表拥有自己的滚动与布局，外框不改变其配置语义。
		Card.m_Measure = [ContentHeight](float) { return ContentHeight; };
		Cards.push_back(std::move(Card));
		RenderSettingsCardPage(MainView, apIds[Page], Cards);
	}

	if(NeedRestart)
	{
		CUIRect RestartWarning, RestartButton;
		RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
		RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
		if(m_NeedRestartUpdate)
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			Ui()->DoLabel(&RestartWarning, Localize("DDNet Client needs to be restarted to complete update!"), 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_RestartButton;
		if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
		{
			if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			{
				m_Popup = POPUP_RESTART;
			}
			else
			{
				Client()->Restart();
			}
		}
	}
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = 20.0f;
	const float MarginPerEntry = 5.0f;
	const float PreviewMargin = 2.5f;
	const float PreviewHeight = 40.0f + 2 * PreviewMargin;
	const float OffY = (SizePerEntry + MarginPerEntry) * (3 + (Alpha ? 1 : 0)) - PreviewHeight;

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 0, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 1, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	for(int i = 0; i < 3 + Alpha; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		Button.VSplitLeft(140.0f, &Label, &Button);
		Label.VMargin(10.0f, &Label);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 1.0f);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];

		// Hue
		if(i == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %.1f° (%03d)", apLabels[i], Color[i] * 360.0f, round_to_int(Color[i] * 255.0f));
		// Lht
		else if(i == 2)
		{
			// handle internal light clamping, see `UnclampLighting`
			float Lht = DarkestLight + Color[i] * (1.0f - DarkestLight);
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Lht * 100.0f, round_to_int(Color[i] * 255.0f));
		}
		// Sat and Alpha
		else
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Color[i] * 100.0f, round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, 12.0f, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}
