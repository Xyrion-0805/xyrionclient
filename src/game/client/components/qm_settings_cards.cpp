// Qm 原版卡片元数据及 Xyrion 功能适配。来源见 docs/xyrion/QMCLIENT_PORT.md.
#include "menus.h"
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <engine/shared/config.h>
#include <game/localization.h>
#include <map>

namespace qm_card_registry
{
	const std::vector<SCardDefault> &Defaults()
	{
		static const std::vector<SCardDefault> Entries = {
			{"deck:general-game", "general", ECardColumn::Left, 0, "Game", ""},
			{"deck:general-language", "general", ECardColumn::Right, 0, "Language", ""},
			{"deck:general-client", "general", ECardColumn::Left, 1, "Client", ""},
			{"deck:general-recording", "general", ECardColumn::Right, 1, "Demo", ""},
			{"deck:player-identity", "player", ECardColumn::Left, 0, "Player", ""},
			{"deck:player-country", "player", ECardColumn::Right, 0, "Choose country flag", ""},
			{"deck:graphics-display", "graphics", ECardColumn::Left, 0, "Graphics display", ""},
			{"deck:graphics-visual", "graphics", ECardColumn::Left, 1, "Visual", ""},
			{"deck:graphics-modes", "graphics", ECardColumn::Right, 0, "Display modes", ""},
			{"deck:graphics-interaction", "graphics", ECardColumn::Right, 1, "Interface animations", ""},
			{"deck:sound-toggle", "sound", ECardColumn::Left, 0, "Sound", ""},
			{"deck:sound-volume", "sound", ECardColumn::Left, 1, "Volume", ""},
			{"deck:ddnet-demo", "ddnet", ECardColumn::Left, 0, "Demo", ""},
			{"deck:ddnet-gameplay", "ddnet", ECardColumn::Left, 1, "Gameplay", ""},
			{"deck:ddnet-background", "ddnet", ECardColumn::Right, 0, "Background", ""},
			{"deck:ddnet-miscellaneous", "ddnet", ECardColumn::Right, 1, "Miscellaneous", ""},
			{"deck:appearance-hud-main", "appearance-hud", ECardColumn::Left, 0, "HUD", ""},
			{"deck:appearance-hud-ddrace", "appearance-hud", ECardColumn::Right, 0, "DDRace HUD", ""},
			{"deck:appearance-chat-settings", "appearance-chat", ECardColumn::Left, 0, "Chat", ""},
			{"deck:appearance-chat-messages", "appearance-chat", ECardColumn::Right, 0, "Messages", ""},
			{"deck:appearance-chat-preview", "appearance-chat", ECardColumn::Left, 1, "Preview", ""},
			{"deck:appearance-name-plate-settings", "appearance-name-plate", ECardColumn::Left, 0, "Name Plate", ""},
			{"deck:appearance-name-plate-hook-strength", "appearance-name-plate", ECardColumn::Right, 1, "Hook Strength", ""},
			{"deck:appearance-name-plate-key-presses", "appearance-name-plate", ECardColumn::Right, 2, "Key Presses", ""},
			{"deck:appearance-name-plate-preview", "appearance-name-plate", ECardColumn::Right, 0, "Preview", ""},
			{"deck:appearance-hook-collision-main", "appearance-hook-collision", ECardColumn::Left, 0, "Hook collision line", ""},
			{"deck:appearance-hook-collision-preview", "appearance-hook-collision", ECardColumn::Right, 0, "Preview", ""},
			{"deck:appearance-info-messages", "appearance-info-messages", ECardColumn::Left, 0, "Info Messages", ""},
			{"deck:appearance-laser-enhanced", "appearance-laser", ECardColumn::Left, 0, "Laser settings", ""},
			{"deck:appearance-laser-colors", "appearance-laser", ECardColumn::Left, 1, "Laser colors", ""},
			{"deck:appearance-laser-preview", "appearance-laser", ECardColumn::Right, 0, "Preview", ""},
			{"xyrion-page:language", "xyrion-page:language", ECardColumn::Full, 0, "Language", ""},
			{"xyrion-page:player", "xyrion-page:player", ECardColumn::Full, 0, "Player", ""},
			{"xyrion-page:tee", "xyrion-page:tee", ECardColumn::Full, 0, "Tee", ""},
			{"xyrion-page:appearance", "xyrion-page:appearance", ECardColumn::Full, 0, "Appearance", ""},
			{"xyrion-page:controls", "xyrion-page:controls", ECardColumn::Full, 0, "Controls", ""},
			{"xyrion-page:graphics", "xyrion-page:graphics", ECardColumn::Full, 0, "Graphics", ""},
			{"xyrion-page:ddnet", "xyrion-page:ddnet", ECardColumn::Full, 0, "DDNet", ""},
			{"xyrion-page:assets", "xyrion-page:assets", ECardColumn::Full, 0, "Assets", ""},
			{"xyrion-page:tclient", "xyrion-page:tclient", ECardColumn::Full, 0, "TClient", ""},
			{"xyrion-page:profiles", "xyrion-page:profiles", ECardColumn::Full, 0, "Profiles", ""},
			{"xyrion-page:configs", "xyrion-page:configs", ECardColumn::Full, 0, "Configs", ""},
			{"xyrion-page:credits", "xyrion-page:credits", ECardColumn::Full, 0, "Credits", ""},
			{"xyrion:trajectory", "xyrion-3", ECardColumn::Left, 0, "武器弹道辅助线", ""},
			{"xyrion:drown", "xyrion-3", ECardColumn::Left, 1, "Gores 队伍落水统计", ""},
			{"xc_sound_gun_fire", "sound", ECardColumn::Left, 2, "武器音效", ""},
			{"xc_sound_player_pain_short", "sound", ECardColumn::Right, 2, "角色音效", ""},
			{"xc_sound_hook_loop", "sound", ECardColumn::Left, 3, "钩子音效", ""},
			{"xc_sound_pickup_health", "sound", ECardColumn::Right, 3, "拾取与提示音", ""},
			{"xc_sound_chat_server", "sound", ECardColumn::Left, 4, "聊天提示音", ""},
			{"xc_sound_ctf_drop", "sound", ECardColumn::Right, 4, "夺旗音效", ""},
			{"xc_sound_menu", "sound", ECardColumn::Left, 5, "菜单音乐", ""},
			{"xyrion:dummy", "xyrion-3", ECardColumn::Left, 2, "分身小窗", ""},
			{"xyrion:steam", "xyrion-3", ECardColumn::Left, 3, "Steam 集成", ""},
			{"xyrion:chat-input", "xyrion-3", ECardColumn::Right, 4, "聊天与输入法", ""},
			{"xyrion:ui", "xyrion-3", ECardColumn::Right, 5, "Qm 界面与动画", ""},
			{"xyrion:console", "xyrion-3", ECardColumn::Right, 6, "控制台外观", ""},
			{"xyrion:browser", "xyrion-0", ECardColumn::Left, 7, "内置浏览器", ""},
			{"xyrion:tee-balance", "xyrion-0", ECardColumn::Right, 1, "动态平衡", ""},
			{"xyrion:pixel-move", "xyrion-0", ECardColumn::Right, 0, "像素移动", ""},
			{"xyrion:input", "xyrion-0", ECardColumn::Left, 8, "输入预测", ""},
			{"xyrion:snaptap", "xyrion-0", ECardColumn::Left, 9, "Snap Tap", ""},
			{"xyrion:moving-tiles", "xyrion-0", ECardColumn::Left, 10, "移动方块预测", ""},
			{"xyrion:race-prediction", "xyrion-0", ECardColumn::Left, 11, "跑图预测", ""},
			{"xyrion:rescue", "xyrion-0", ECardColumn::Right, 12, "钩救求解", ""},
			{"xyrion:ai", "xyrion-0", ECardColumn::Right, 13, "AI 自动回复", ""},
			{"xyrion:glass", "xyrion-1", ECardColumn::Left, 14, "界面与液态玻璃", ""},
			{"xyrion:aspect", "xyrion-1", ECardColumn::Left, 15, "屏幕比例", ""},
			{"xyrion:video", "xyrion-1", ECardColumn::Left, 16, "视频背景", ""},
			{"xyrion:media", "xyrion-1", ECardColumn::Right, 18, "灵动岛", ""},
			{"xyrion:bubbles", "xyrion-1", ECardColumn::Right, 19, "聊天气泡", ""},
			{"xyrion-tclient:settings-0", "tclient-settings", ECardColumn::Left, 0, "Visual", ""},
			{"xyrion-tclient:settings-1", "tclient-settings", ECardColumn::Left, 1, "Anti Latency Tools", ""},
			{"xyrion-tclient:settings-2", "tclient-settings", ECardColumn::Left, 2, "Anti Ping Smoothing", ""},
			{"xyrion-tclient:settings-3", "tclient-settings", ECardColumn::Left, 3, "Auto execute", ""},
			{"xyrion-tclient:settings-4", "tclient-settings", ECardColumn::Left, 4, "Teams", ""},
			{"xyrion-tclient:settings-5", "tclient-settings", ECardColumn::Left, 5, "Voting", ""},
			{"xyrion-tclient:settings-6", "tclient-settings", ECardColumn::Left, 6, "Auto Reply", ""},
			{"xyrion-tclient:settings-7", "tclient-settings", ECardColumn::Left, 7, "Player Indicator", ""},
			{"xyrion-tclient:settings-8", "tclient-settings", ECardColumn::Left, 8, "Pet", ""},
			{"xyrion-tclient:settings-9", "tclient-settings", ECardColumn::Right, 9, "HUD", ""},
			{"xyrion-tclient:settings-10", "tclient-settings", ECardColumn::Right, 10, "Frozen Tee Display", ""},
			{"xyrion-tclient:settings-11", "tclient-settings", ECardColumn::Right, 11, "Tile Outlines", ""},
			{"xyrion-tclient:settings-12", "tclient-settings", ECardColumn::Right, 12, "Ghost Tools", ""},
			{"xyrion-tclient:settings-13", "tclient-settings", ECardColumn::Right, 13, "Rainbow", ""},
			{"xyrion-tclient:settings-14", "tclient-settings", ECardColumn::Right, 14, "Tee Trails", ""},
			{"xyrion-tclient:settings-15", "tclient-settings", ECardColumn::Right, 15, "Background Draw", ""},
			{"xyrion-tclient:settings-16", "tclient-settings", ECardColumn::Right, 16, "Finish Name", ""},
			{"xyrion-tclient:settings-17", "tclient-settings", ECardColumn::Right, 17, "Translate", ""},
			{"xyrion-tclient:bindchat", "xyrion-tclient:bindchat", ECardColumn::Full, 0, "Chat Binds", ""},
			{"xyrion-tclient:bindwheel", "xyrion-tclient:bindwheel", ECardColumn::Full, 0, "Bind Wheel", ""},
			{"xyrion-tclient:warlist", "xyrion-tclient:warlist", ECardColumn::Full, 0, "War List", ""},
			{"xyrion-tclient:statusbar", "xyrion-tclient:statusbar", ECardColumn::Full, 0, "Status Bar", ""},
			{"xyrion-tclient:info", "xyrion-tclient:info", ECardColumn::Full, 0, "Info", ""},
		};
		return Entries;
	}
	const SCardDefault *FindByStableId(const char *pId)
	{
		for(const auto &Entry : Defaults())
			if(pId && str_comp(Entry.m_pStableId, pId) == 0)
				return &Entry;
		return nullptr;
	}
}

SCardMotionSpec CMenus::SettingsCardMotionSpec() const
{
	return ResolveCardMotionSpec(g_Config.m_QmUiMotionLevel, g_Config.m_QmUiListEntryAnimations != 0,
		g_Config.m_QmUiCardHeightAnimations != 0, g_Config.m_QmUiCardReflowAnimations != 0, g_Config.m_QmExtraAnimations != 0);
}

SSettingsCardDeckVisualOptions CMenus::SettingsCardDeckVisualOptions() const
{
	SSettingsCardDeckVisualOptions Options;
	Options.m_RainbowTitles = g_Config.m_QmUiCardRainbowTitles != 0;
	Options.m_AlwaysShowBorders = g_Config.m_QmUiCardBorders != 0;
	Options.m_BorderColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiCardBorderColor, true));
	Options.m_SurfaceColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiCardColor)).WithAlpha(g_Config.m_QmUiCardOpacity / 100.0f);
	Options.m_UseSurfaceColor = true;
	return Options;
}

SSettingsCardDefinition CMenus::SettingsCardDefinition(const char *pId, const char *pTitle, float InitialHeight, FSettingsCardRenderMeasured Render)
{
	SSettingsCardDefinition Definition;
	Definition.m_Spec = {pId, pTitle};
	const std::string Id(pId);
	m_SettingsCardContentHeights.try_emplace(Id, InitialHeight);
	Definition.m_Measure = [this, Id](float) { return m_SettingsCardContentHeights.at(Id); };
	Definition.m_RenderMeasured = [this, Id, Render = std::move(Render)](CUIRect &Content) {
		const float StartY = Content.y;
		Render(Content);
		m_SettingsCardContentHeights[Id] = std::max(0.0f, Content.y - StartY);
	};
	return Definition;
}

void CMenus::RenderSettingsLegacyPanel(CUIRect MainView, const char *pId, const char *pTitle, FSettingsCardRender Render)
{
	const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float ChromeHeight = BuildSettingsCardFrame({0, 0, MainView.w, 0}, {}, 0.0f, Metrics.m_UiScale).m_Rect.h;
	const float Height = std::max(1.0f, MainView.h - ChromeHeight - 2.0f * ui_token::settings::PAGE_INSET * Metrics.m_UiScale);
	auto Card = SettingsCardDefinition(pId, pTitle, Height, [Render = std::move(Render)](CUIRect &Content) {
		Render(Content);
		Content.y += Content.h;
		Content.h = 0.0f;
	});
	Card.m_Measure = [Height](float) { return Height; };
	RenderSettingsCardPage(MainView, pId, {std::move(Card)});
}

void CMenus::RenderSettingsCardPage(CUIRect MainView, const char *pTab, const std::vector<SSettingsCardDefinition> &Cards)
{
	// 页面只提供内容；滚动、折叠、拖放、连续入场和高度重排均由原版 Deck 负责。
	if(!m_SettingsCardOrderLoaded || m_SettingsCardOrderSource != g_Config.m_QmGlobalCardOrder)
	{
		std::vector<qm_card_order::SEntry> Defaults;
		for(const auto &Entry : qm_card_registry::Defaults())
			Defaults.push_back({Entry.m_pStableId, Entry.m_pDefaultTab, Entry.m_DefaultColumn == qm_card_registry::ECardColumn::Full ? 0 : Entry.m_DefaultColumn == qm_card_registry::ECardColumn::Left ? 1 : 2, Entry.m_DefaultOrder});
		m_SettingsCardOrderModel.LoadMerged(g_Config.m_QmGlobalCardOrder, Defaults);
		m_SettingsCardOrderSource = g_Config.m_QmGlobalCardOrder;
		m_SettingsCardOrderLoaded = true;
	}
	const uint64_t ViewKey = (static_cast<uint64_t>(str_quickhash(pTab)) << 1) | (m_Dummy ? 1ull : 0ull);
	if(m_SettingsCardDisplayState.EnterView(ViewKey))
		m_SettingsCardDeck.BeginDisplayCycle(++m_SettingsCardDisplayCycle, true);

	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const auto Layout = ResolveSettingsPageLayout(MainView, false, Metrics.m_UiScale);
	const IUiContext Context = SettingsUiContext(pTab, Metrics.m_UiScale);
	static std::map<std::string, CScrollRegion> s_ScrollRegions;
	const SQmScrollRequest Request{EQmScrollProfile::SETTINGS_OUTER};
	CScrollRegionParams Params = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(Request, Metrics.m_UiScale, 0.0f));
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = &Params;
	const auto Result = m_SettingsCardDeck.Render(Context, Layout, pTab, Cards, m_SettingsCardOrderModel, &s_ScrollRegions[pTab], InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(Result.m_OrderChanged && m_SettingsCardOrderModel.Serialize(g_Config.m_QmGlobalCardOrder, sizeof(g_Config.m_QmGlobalCardOrder)))
	{
		m_SettingsCardOrderSource = g_Config.m_QmGlobalCardOrder;
		m_SettingsCardOrderModel.ClearDirty();
	}
}
