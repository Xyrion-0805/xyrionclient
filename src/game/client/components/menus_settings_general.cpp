/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/menu_background.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

void CMenus::RenderSettingsGeneral(CUIRect MainView)
{
	const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
	const auto Row = [&](CUIRect &Content) {
		CUIRect Button;
		Content.HSplitTop(Metrics.m_LineHeight, &Button, &Content);
		Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
		return Button;
	};
	const auto Toggle = [&](CUIRect &Content, int &Value, const char *pText) {
		const CUIRect Button = Row(Content);
		if(DoButton_CheckBox(&Value, pText, Value, &Button, Metrics.m_BodySize))
			Value ^= 1;
	};
	std::vector<SSettingsCardDefinition> Cards;
	Cards.push_back(SettingsCardDefinition("deck:general-game", Localize("Game"), 4.0f * Metrics.m_RowStep, [&](CUIRect &Content) {
		CUIRect Button = Row(Content);
		const bool IsDyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
		if(DoButton_CheckBox(&g_Config.m_ClDyncam, Localize("Dynamic Camera"), IsDyncam, &Button, Metrics.m_BodySize))
		{
			g_Config.m_ClDyncam = IsDyncam ? 0 : 1;
			if(IsDyncam)
				g_Config.m_ClMouseFollowfactor = 0;
		}
		if(g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0)
		{
			Button = Row(Content);
			if(DoButton_CheckBox(&g_Config.m_ClDyncamSmoothness, Localize("Smooth Dynamic Camera"), g_Config.m_ClDyncamSmoothness, &Button, Metrics.m_BodySize))
			{
				if(g_Config.m_ClDyncamSmoothness)
					g_Config.m_ClDyncamSmoothness = 0;
				else
				{
					g_Config.m_ClDyncamSmoothness = 50;
					g_Config.m_ClDyncamStabilizing = 50;
				}
			}
		}
		Toggle(Content, g_Config.m_ClAutoswitchWeapons, Localize("Switch weapon on pickup"));
		Toggle(Content, g_Config.m_ClAutoswitchWeaponsOutOfAmmo, Localize("Switch weapon when out of ammo"));
	}));
	const float LanguageHeight = ResolveSettingsGeneralLanguageListGeometry((int)g_Localization.Languages().size(), Metrics).m_ContentHeight;
	Cards.push_back(SettingsCardDefinition("deck:general-language", Localize("Language"), LanguageHeight, [&](CUIRect &Content) {
		CUIRect List;
		Content.HSplitTop(LanguageHeight, &List, &Content);
		RenderLanguageSelection(List);
	}));
	const float ThemeHeight = ResolveSettingsGeneralThemeListGeometry((int)GameClient()->m_MenuBackground.GetThemes().size(), Metrics).m_ContentHeight;
	Cards.push_back(SettingsCardDefinition("deck:general-client", Localize("Client"), 8.0f * Metrics.m_RowStep + ThemeHeight, [&](CUIRect &Content) {
		Toggle(Content, g_Config.m_ClSkipStartMenu, Localize("Skip the main menu"));
		CUIRect Button = Row(Content);
		char aHz[32];
		str_format(aHz, sizeof(aHz), " %s", Localize("Hz", "Hertz"));
		Ui()->DoScrollbarOption(&g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, &Button, Localize("Update Rate"), 10, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aHz);
		Button = Row(Content);
		static int s_LowerRefreshRate;
		if(DoButton_CheckBox(&s_LowerRefreshRate, Localize("Save power by lowering update rate (higher input latency)"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button, Metrics.m_BodySize))
			g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;
		const char *apNames[] = {Localize("Settings file"), Localize("Saves file"), Localize("Config directory"), Localize("Themes directory")};
		const char *apPaths[] = {s_aConfigDomains[ConfigDomain::DDNET].m_aConfigPath, SAVES_FILE, "", "themes"};
		static CButtonContainer s_aPaths[4];
		for(int i = 0; i < 4; ++i)
		{
			Button = Row(Content);
			if(DoButton_Menu(&s_aPaths[i], apNames[i], 0, &Button))
			{
				char aPath[IO_MAX_PATH_LENGTH];
				if(i == 3)
					Storage()->CreateFolder("themes", IStorage::TYPE_SAVE);
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, apPaths[i], aPath, sizeof(aPath));
				Client()->ViewFile(aPath);
			}
		}
		CUIRect Themes;
		Content.HSplitTop(ThemeHeight, &Themes, &Content);
		RenderThemeSelection(Themes);
	}));
	Cards.push_back(SettingsCardDefinition("deck:general-recording", Localize("Demo"), 8.0f * Metrics.m_RowStep, [&](CUIRect &Content) {
		const auto Record = [&](int &Enabled, int &Max, const char *pLabel, const char *pLimit) {
			Toggle(Content, Enabled, pLabel);
			if(Enabled)
			{
				const CUIRect Button = Row(Content);
				Ui()->DoScrollbarOption(&Max, &Max, &Button, pLimit, 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE);
			}
		};
		Record(g_Config.m_ClAutoDemoRecord, g_Config.m_ClAutoDemoMax, Localize("Automatically record demos"), Localize("Max demos"));
		Record(g_Config.m_ClAutoScreenshot, g_Config.m_ClAutoScreenshotMax, Localize("Automatically take game over screenshot"), Localize("Max Screenshots"));
		Record(g_Config.m_ClAutoStatboardScreenshot, g_Config.m_ClAutoStatboardScreenshotMax, Localize("Automatically take statboard screenshot"), Localize("Max Screenshots"));
		Record(g_Config.m_ClAutoCSV, g_Config.m_ClAutoCSVMax, Localize("Automatically create statboard csv"), Localize("Max CSVs"));
	}));
	RenderSettingsCardPage(MainView, "general", Cards);
}

void CMenus::RenderThemeSelection(CUIRect MainView)
{
	const std::vector<CTheme> &vThemes = GameClient()->m_MenuBackground.GetThemes();

	int SelectedTheme = -1;
	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		if(str_comp(vThemes[i].m_Name.c_str(), g_Config.m_ClMenuMap) == 0)
		{
			SelectedTheme = i;
			break;
		}
	}
	const int OldSelected = SelectedTheme;

	static CListBox s_ListBox;
	s_ListBox.DoHeader(&MainView, Localize("Theme"), 20.0f);
	s_ListBox.DoStart(20.0f, vThemes.size(), 1, 3, SelectedTheme);

	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		const CTheme &Theme = vThemes[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&Theme.m_Name, i == SelectedTheme);

		if(!Item.m_Visible)
			continue;

		CUIRect Icon, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h * 2.0f, &Icon, &Label);

		// draw icon if it exists
		if(Theme.m_IconTexture.IsValid())
		{
			Icon.VMargin(6.0f, &Icon);
			Icon.HMargin(3.0f, &Icon);
			Graphics()->TextureSet(Theme.m_IconTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Icon.x, Icon.y, Icon.w, Icon.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		char aName[128];
		if(Theme.m_Name.empty())
			str_copy(aName, "(none)");
		else if(str_comp(Theme.m_Name.c_str(), "auto") == 0)
			str_copy(aName, "(seasons)");
		else if(str_comp(Theme.m_Name.c_str(), "rand") == 0)
			str_copy(aName, "(random)");
		else if(Theme.m_HasDay && Theme.m_HasNight)
			str_copy(aName, Theme.m_Name.c_str());
		else if(Theme.m_HasDay && !Theme.m_HasNight)
			str_format(aName, sizeof(aName), "%s (day)", Theme.m_Name.c_str());
		else if(!Theme.m_HasDay && Theme.m_HasNight)
			str_format(aName, sizeof(aName), "%s (night)", Theme.m_Name.c_str());
		else // generic
			str_copy(aName, Theme.m_Name.c_str());

		Ui()->DoLabel(&Label, aName, 16.0f * CUi::ms_FontmodHeight, TEXTALIGN_ML);
	}

	SelectedTheme = s_ListBox.DoEnd();

	if(OldSelected != SelectedTheme)
	{
		const CTheme &Theme = vThemes[SelectedTheme];
		str_copy(g_Config.m_ClMenuMap, Theme.m_Name.c_str());
		GameClient()->m_MenuBackground.LoadMenuBackground(Theme.m_HasDay, Theme.m_HasNight);
	}
}
