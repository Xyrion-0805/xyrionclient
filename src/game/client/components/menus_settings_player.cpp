/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/console.h>
#include <game/client/components/countryflags.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <vector>

void CMenus::RenderSettingsPlayer(CUIRect MainView)
{
	CUIRect TabBar, ChangeInfo, QuickSearch;
	const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
	const auto SubTabs = ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale);
	TabBar = SubTabs.m_TabBarRect;
	MainView = SubTabs.m_ContentRect;
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.f);

	static CButtonContainer s_aPlayerTabs[2];
	const char *apPlayerTabs[] = {Localize("Player"), Localize("Dummy")};
	const int NewPlayerTab = DoSegmentedChoiceInternal(s_aPlayerTabs, apPlayerTabs, 2, m_Dummy ? 1 : 0, TabBar, ui_token::radius::PILL, nullptr, 14.0f, nullptr, nullptr, true);
	if((NewPlayerTab != 0) != m_Dummy)
	{
		Ui()->ClosePopupMenus();
		Ui()->SetActiveItem(nullptr);
		m_Dummy = NewPlayerTab != 0;
	}

	if(Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_aNextChangeInfo[m_Dummy] > Client()->GameTick(m_Dummy))
	{
		char aChangeInfo[128], aTimeLeft[32];
		str_format(aTimeLeft, sizeof(aTimeLeft), Localize("%ds left"), (GameClient()->m_aNextChangeInfo[m_Dummy] - Client()->GameTick(m_Dummy) + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aChangeInfo, sizeof(aChangeInfo), "%s: %s", Localize("Player info change cooldown"), aTimeLeft);
		Ui()->DoLabel(&ChangeInfo, aChangeInfo, 10.f, TEXTALIGN_ML);
	}

	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;

	int *pCountry;
	if(!m_Dummy)
	{
		pCountry = &g_Config.m_PlayerCountry;
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		pCountry = &g_Config.m_ClDummyCountry;
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	std::vector<SSettingsCardDefinition> Cards;
	Cards.push_back(SettingsCardDefinition("deck:player-identity", Localize("Player"), 50.0f, [&](CUIRect &MainView) {
		// player name
		CUIRect Button, Label;
		MainView.HSplitTop(Metrics.m_LineHeight, &Button, &MainView);
		Button.VSplitLeft(std::min(80.0f, Button.w * 0.35f), &Label, &Button);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
		Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
		if(Ui()->DoEditBox(&s_NameInput, &Button, 14.0f))
		{
			SetNeedSendInfo();
		}

		// player clan
		MainView.HSplitTop(Metrics.m_LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(Metrics.m_LineHeight, &Button, &MainView);
		Button.VSplitLeft(std::min(80.0f, Button.w * 0.35f), &Label, &Button);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
		Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
		if(Ui()->DoEditBox(&s_ClanInput, &Button, 14.0f))
		{
			SetNeedSendInfo();
		}

	}));
	Cards.push_back(SettingsCardDefinition("deck:player-country", Localize("Choose country flag"), 320.0f, [&](CUIRect &Content) {
		CUIRect MainView;
		Content.HSplitTop(320.0f, &MainView, &Content);
		// country flag selector
		static CLineInputBuffered<25> s_FlagFilterInput;

		class CCountryFlagEntry
		{
		public:
			const CCountryFlags::CCountryFlag *m_pFlag;
			std::optional<std::pair<int, int>> m_NameMatch;
		};
		std::vector<CCountryFlagEntry> vFilteredFlags;
		for(size_t i = 0; i < GameClient()->m_CountryFlags.Num(); ++i)
		{
			const CCountryFlags::CCountryFlag &Entry = GameClient()->m_CountryFlags.GetByIndex(i);
			if(!s_FlagFilterInput.IsEmpty())
			{
				const char *pNameMatchEnd;
				const char *pNameMatchStart = str_utf8_find_nocase(Entry.m_aCountryCodeString, s_FlagFilterInput.GetString(), &pNameMatchEnd);
				if(pNameMatchStart != nullptr)
				{
					vFilteredFlags.emplace_back(&Entry, std::make_pair<int, int>(pNameMatchStart - Entry.m_aCountryCodeString, pNameMatchEnd - pNameMatchStart));
				}
			}
			else
			{
				vFilteredFlags.emplace_back(&Entry, std::nullopt);
			}
		}

		CUIRect Label;
		MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
		MainView.HSplitBottom(5.0f, &MainView, nullptr);
		QuickSearch.VSplitLeft(220.0f, &QuickSearch, nullptr);

		int OldSelected = -1;
		static CListBox s_ListBox;
		s_ListBox.DoStart(48.0f, vFilteredFlags.size(), std::max(1, (int)(MainView.w / 70.0f)), 3, OldSelected, &MainView);

		for(size_t i = 0; i < vFilteredFlags.size(); i++)
		{
			const CCountryFlagEntry &Entry = vFilteredFlags[i];

			if(Entry.m_pFlag->m_CountryCode == *pCountry)
				OldSelected = i;

			const CListboxItem Item = s_ListBox.DoNextItem(&Entry.m_pFlag->m_CountryCode, OldSelected >= 0 && (size_t)OldSelected == i);
			if(!Item.m_Visible)
				continue;

			CUIRect FlagRect;
			Item.m_Rect.Margin(5.0f, &FlagRect);
			FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
			Label.HSplitTop(2.0f, nullptr, &Label);
			const float OldWidth = FlagRect.w;
			FlagRect.w = FlagRect.h * 2;
			FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
			GameClient()->m_CountryFlags.Render(Entry.m_pFlag->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

			if(Entry.m_pFlag->m_Texture.IsValid() || Entry.m_pFlag->m_CountryCode == CountryCode::DEFAULT)
			{
				SLabelProperties Props;
				Props.m_MaxWidth = Label.w - 5.0f;
				if(Entry.m_NameMatch.has_value())
				{
					const auto [MatchStart, MatchLength] = Entry.m_NameMatch.value();
					Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
				}
				Ui()->DoLabel(&Label, Entry.m_pFlag->m_aCountryCodeString, 10.0f, TEXTALIGN_MC, Props);
			}
		}

		const int NewSelected = s_ListBox.DoEnd();
		if(NewSelected >= 0 && NewSelected < (int)vFilteredFlags.size() && OldSelected != NewSelected)
		{
			*pCountry = vFilteredFlags[NewSelected].m_pFlag->m_CountryCode;
			SetNeedSendInfo();
		}

		if(GameClient()->m_CountryFlags.Num() > 0 && vFilteredFlags.empty())
		{
			CUIRect FilterLabel, ResetButton;
			MainView.HMargin((MainView.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &FilterLabel);
			FilterLabel.HSplitTop(16.0f, &FilterLabel, &ResetButton);
			ResetButton.HSplitTop(8.0f, nullptr, &ResetButton);
			ResetButton.VMargin((ResetButton.w - 200.0f) / 2.0f, &ResetButton);
			Ui()->DoLabel(&FilterLabel, Localize("No country flags match your filter criteria"), 16.0f, TEXTALIGN_MC);
			static CButtonContainer s_ResetButton;
			if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
			{
				s_FlagFilterInput.Clear();
			}
		}

		Ui()->DoEditBox_Search(&s_FlagFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
	}));
	RenderSettingsCardPage(MainView, "player", Cards);
}
