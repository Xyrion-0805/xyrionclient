/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <engine/shared/config.h>
#include <engine/sound.h>

#include <game/localization.h>
#include <game/client/gameclient.h>
#include <game/client/components/sounds.h>
#include <generated/client_data.h>

void CMenus::RenderSettingsSound(CUIRect MainView)
{
	const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
	m_NeedRestartSound = g_Config.m_SndEnable && !Sound()->IsSoundEnabled();
	std::vector<SSettingsCardDefinition> Cards;
	Cards.push_back(SettingsCardDefinition("deck:sound-toggle", Localize("Sound"), 10.0f * Metrics.m_RowStep, [&](CUIRect &Content) {
		const auto Toggle = [&](int &Value, const char *pText, bool Music = false) {
			CUIRect Row;
			Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
			Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
			if(DoButton_CheckBox(&Value, pText, Value, &Row, Metrics.m_BodySize))
			{
				Value ^= 1;
				if(Music)
					UpdateMusicState();
			}
		};
		Toggle(g_Config.m_SndEnable, Localize("Use sounds"), true);
		if(!g_Config.m_SndEnable)
			return;
		Toggle(g_Config.m_SndMusic, Localize("Play background music"), true);
		Toggle(g_Config.m_SndNonactiveMute, Localize("Mute when not active"));
		Toggle(g_Config.m_SndGame, Localize("Enable game sounds"));
		Toggle(g_Config.m_SndGun, Localize("Enable gun sound"));
		Toggle(g_Config.m_SndLongPain, Localize("Enable long pain sound (used when shooting in freeze)"));
		Toggle(g_Config.m_SndServerMessage, Localize("Enable server message sound"));
		Toggle(g_Config.m_SndChat, Localize("Enable regular chat sound"));
		Toggle(g_Config.m_SndTeamChat, Localize("Enable team chat sound"));
		Toggle(g_Config.m_SndHighlight, Localize("Enable highlighted chat sound"));
	}));
	if(g_Config.m_SndEnable)
	{
		Cards.push_back(SettingsCardDefinition("deck:sound-volume", Localize("Volume"), 5.0f * Metrics.m_RowStep, [&](CUIRect &Content) {
			int *apValues[] = {&g_Config.m_SndVolume, &g_Config.m_SndGameVolume, &g_Config.m_SndChatVolume, &g_Config.m_SndMapVolume, &g_Config.m_SndBackgroundMusicVolume};
			const char *apLabels[] = {Localize("Sound volume"), Localize("Game sound volume"), Localize("Chat sound volume"), Localize("Map sound volume"), Localize("Background music volume")};
			for(int i = 0; i < 5; ++i)
			{
				CUIRect Row;
				Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
				Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
				Ui()->DoScrollbarOption(apValues[i], apValues[i], &Row, apLabels[i], 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
			}
		}));
	}
	if(g_Config.m_SndEnable)
	{
		const auto Toggles = CSounds::SoundToggles();
		for(size_t Begin = 0; Begin < Toggles.size();)
		{
			size_t End = Begin + 1;
			while(End < Toggles.size() && str_comp(Toggles[Begin].m_pGroup, Toggles[End].m_pGroup) == 0)
				++End;
			Cards.push_back(SettingsCardDefinition(Toggles[Begin].m_pId, Toggles[Begin].m_pGroup, (End - Begin) * Metrics.m_RowStep, [&, Begin, End](CUIRect &Content) {
				const auto Options = CSounds::SoundToggles();
				for(size_t Index = Begin; Index < End; ++Index)
				{
					const auto &Option = Options[Index];
					CUIRect Row;
					Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
					Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
					if(DoButton_CheckBox(Option.m_pEnabled, Option.m_pLabel, *Option.m_pEnabled, &Row, Metrics.m_BodySize))
					{
						*Option.m_pEnabled ^= 1;
						if(!*Option.m_pEnabled) GameClient()->m_Sounds.Stop(Option.m_Sound);
						if(Option.m_Sound == SOUND_MENU) UpdateMusicState();
					}
				}
			}));
			Begin = End;
		}
	}
	RenderSettingsCardPage(MainView, "sound", Cards);
}
