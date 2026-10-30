/* Copyright © 2026 BestProject Team */
#include "hud_layout.h"


#include <base/math.h>
#include <base/str.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>

// bestclient

// bestclient

#include <algorithm>
#include <cstdlib>

namespace HudLayout
{

namespace
{

static const SModuleLayout gs_aModuleLayouts[MODULE_COUNT] = {
	{0.0f, 60.0f, 100, 0, true, true, 0x66000000U, 100},
	{304.0f, 0.0f, 100, 0, true, true, 0x66000000U, 100},
	{470.0f, 205.0f, 100, 0, true, true, 0x66000000U, 100},
	{100.0f, 3.0f, 100, 0, true, false, 0x66000000U, 100},
	{500.0f, 5.0f, 100, 0, true, false, 0x66000000U, 100},
	{500.0f, 20.0f, 100, 0, true, false, 0x66000000U, 100},
	{233.0f, 64.0f, 100, 0, true, false, 0x66000000U, 100},
	{220.0f, 240.0f, 100, 0, true, false, 0x66000000U, 100},
	{110.0f, 2.0f, 100, 0, true, true, 0x66000000U, 100},
	{500.0f, 141.0f, 100, 0, true, true, 0x66000000U, 100},
	{460.0f, 229.0f, 100, 0, true, true, 0x40000000U, 100},
	{0.0f, 100.0f, 100, 0, true, false, 0x66000000U, 100},
	{136.0f, 0.0f, 100, 0, true, false, 0x66000000U, 100},
	{5.0f, 278.0f, 100, 0, true, false, 0x66000000U, 100},
	{0.0f, 60.0f, 100, 0, true, true, 0x66000000U, 100},
	{250.0f, 200.0f, 65, 0, true, true, 0x66000000U, 100},
	{490.0f, 5.0f, 100, 0, true, false, 0x66000000U, 100},
	{0.0f, 129.0f, 100, 0, true, true, 0x66000000U, 100},
	{0.0f, 152.0f, 100, 0, true, false, 0x66000000U, 100},
	{84.5f, 152.0f, 100, 0, true, false, 0x66000000U, 100},
	{484.0f, 172.0f, 100, 0, true, true, 0x66000000U, 100},
	{250.0f, 258.0f, 100, 0, true, true, 0x66000000U, 100},
	{200.0f, 168.0f, 100, 0, true, true, 0xE6000033U, 100},
};

static const char *gs_apModuleIds[MODULE_COUNT] = {
	"mini_vote",
	"frozen_hud",
	"movement_info",
	"notify_last",
	"fps",
	"ping",
	"game_timer",
	"hook_combo",
	"local_time",
	"spectator_count",
	"score",
	"voice_hud",
	"voice_mute_icons",
	"chat",
	"votes",
	"lock_cam",
	"killfeed",
	"finish_prediction",
	"keystrokes_keyboard",
	"keystrokes_mouse",
	"dummy_actions",
	"swap_timer",
	"edge_info",
};

static const char *gs_apModuleNames[MODULE_COUNT] = {
	"Mini Vote",
	"Frozen HUD",
	"Movement Info",
	"Notify Last",
	"FPS",
	"Ping",
	"Game Timer",
	"Hook Combo",
	"Local Time",
	"Spectator Count",
	"Score",
	"Voice HUD",
	"Voice Mute Icons",
	"Ingame Chat",
	"Votes",
	"Lock Cam",
	"Killfeed",
	"Finish Prediction",
	"Keyboard",
	"Mouse",
	"Dummy Actions",
	"Swap timer",
	"Edge Info",
};

static SModuleLayout gs_aRuntimeModuleLayouts[MODULE_COUNT];
static bool gs_RuntimeLayoutsInitialized = false;
static bool gs_ConfigCallbackRegistered = false;
static bool gs_ConsoleCommandRegistered = false;
static bool gs_LegacyMigrated = false;

void EnsureRuntimeLayouts()
{
	if(gs_RuntimeLayoutsInitialized)
		return;
	for(int i = 0; i < MODULE_COUNT; ++i)
		gs_aRuntimeModuleLayouts[i] = gs_aModuleLayouts[i];
	gs_RuntimeLayoutsInitialized = true;
}

bool HasRuntimeOverrideInternal(EModule Module)
{
	EnsureRuntimeLayouts();
	const SModuleLayout &Runtime = gs_aRuntimeModuleLayouts[Module];
	const SModuleLayout &Default = gs_aModuleLayouts[Module];
	return Runtime.m_X != Default.m_X ||
	       Runtime.m_Y != Default.m_Y ||
	       Runtime.m_Scale != Default.m_Scale ||
	       Runtime.m_Mode != Default.m_Mode ||
	       Runtime.m_Enabled != Default.m_Enabled ||
	       Runtime.m_BackgroundEnabled != Default.m_BackgroundEnabled ||
	       Runtime.m_BackgroundColor != Default.m_BackgroundColor ||
	       Runtime.m_Alpha != Default.m_Alpha;
}

bool HasDynamicDefault(EModule Module)
{
	switch(Module)
	{
	case MODULE_MINI_VOTE:
	case MODULE_FROZEN_HUD:
	case MODULE_MOVEMENT_INFO:
	case MODULE_NOTIFY_LAST:
	case MODULE_FPS:
	case MODULE_PING:
	case MODULE_GAME_TIMER:
	case MODULE_HOOK_COMBO:
	case MODULE_LOCAL_TIME:
	case MODULE_KEYSTROKES_MOUSE:
	case MODULE_DUMMY_ACTIONS:
	case MODULE_SWAP_TIMER:
		return true;
	default:
		return false;
	}
}

SModuleLayout DynamicDefaultLayout(EModule Module, float HudWidth, float HudHeight)
{
	SModuleLayout Layout = gs_aModuleLayouts[Module];
	switch(Module)
	{
	case MODULE_MINI_VOTE:
		Layout.m_X = 0.0f;
		Layout.m_Y = 60.0f;
		break;
	case MODULE_FROZEN_HUD:
		Layout.m_X = HudWidth / 2.0f + 60.0f * (HudWidth / HudHeight) / 1.78f;
		Layout.m_Y = 0.0f;
		break;
	case MODULE_MOVEMENT_INFO:
		Layout.m_X = (float)round_to_int(HudWidth - 62.0f);
		Layout.m_Y = 205.0f;
		break;
	case MODULE_NOTIFY_LAST:
		Layout.m_X = (float)round_to_int(HudWidth * 0.2f);
		Layout.m_Y = (float)round_to_int(HudHeight * 0.01f);
		break;
	case MODULE_FPS:
		Layout.m_X = (float)round_to_int(HudWidth - 26.0f);
		Layout.m_Y = 5.0f;
		break;
	case MODULE_PING:
		Layout.m_X = (float)round_to_int(HudWidth - 26.0f);
		Layout.m_Y = 20.0f;
		break;
	case MODULE_GAME_TIMER:
		Layout.m_X = (float)round_to_int(HudWidth * 0.5f - 22.0f);
		Layout.m_Y = -2.0f;
		break;
	case MODULE_LOCAL_TIME:
		Layout.m_X = HudWidth / 7.0f * 3.0f;
		Layout.m_Y = 0.0f;
		break;
	case MODULE_KEYSTROKES_MOUSE:
		// bestclient
		if(!HasRuntimeOverrideInternal(MODULE_KEYSTROKES_KEYBOARD))
			Layout.m_X = 84.5f + 5.0f;
		// bestclient
		Layout.m_Y = 152.0f;
		break;
	case MODULE_DUMMY_ACTIONS:
		Layout.m_X = (float)round_to_int(HudWidth - 16.0f);
		Layout.m_Y = 172.0f;
		break;
	case MODULE_SWAP_TIMER:
		Layout.m_X = (float)round_to_int(HudWidth * 0.5f);
		Layout.m_Y = 258.0f;
		break;

	default:
		break;
	}
	return Layout;
}

SModuleLayout ResolveBaseLayout(EModule Module, float HudWidth, float HudHeight)
{
	SModuleLayout Layout;
	if(HasDynamicDefault(Module) && !HasRuntimeOverrideInternal(Module))
	{
		Layout = DynamicDefaultLayout(Module, HudWidth, HudHeight);
	}
	else
	{
		EnsureRuntimeLayouts();
		Layout = gs_aRuntimeModuleLayouts[Module];
		Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
	}
	return Layout;
}

void ApplyLayout(EModule Module, const SModuleLayout &Layout)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module] = Layout;
	gs_aRuntimeModuleLayouts[Module].m_Scale = std::clamp(Layout.m_Scale, 25, 300);
	gs_aRuntimeModuleLayouts[Module].m_Alpha = std::clamp(Layout.m_Alpha, 0, 100);
	if(Module == MODULE_CHAT)
		g_Config.m_BcHudChatScale = gs_aRuntimeModuleLayouts[Module].m_Scale;
}

void ConHudLayoutSet(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	if(pResult->NumArguments() < 7)
		return;

	EModule Module = MODULE_COUNT;
	const char *pModuleArg = pResult->GetString(0);
	if(pModuleArg && pModuleArg[0] != '\0')
	{
		char *pEnd = nullptr;
		const long Index = std::strtol(pModuleArg, &pEnd, 10);
		if(pEnd != pModuleArg && *pEnd == '\0')
		{
			if(Index < 0 || Index >= MODULE_COUNT)
				return;
			Module = (EModule)Index;
		}
		else
		{
			Module = ModuleFromId(pModuleArg);
			if(Module == MODULE_COUNT)
				return;
		}
	}
	if(Module == MODULE_GAME_TIMER)
		return;

	EnsureRuntimeLayouts();
	SModuleLayout Layout = gs_aRuntimeModuleLayouts[Module];
	Layout.m_X = pResult->GetFloat(1);
	Layout.m_Y = pResult->GetFloat(2);
	Layout.m_Scale = std::clamp(pResult->GetInteger(3), 25, 300);
	Layout.m_Mode = pResult->GetInteger(4);
	Layout.m_BackgroundEnabled = pResult->GetInteger(5) != 0;
	// bestclient
	Layout.m_BackgroundColor = (unsigned)str_toulong_base(pResult->GetString(6), 10);
	// bestclient
	if(pResult->NumArguments() > 7)
		Layout.m_Enabled = pResult->GetInteger(7) != 0;
	if(pResult->NumArguments() > 8)
		Layout.m_Alpha = std::clamp(pResult->GetInteger(8), 0, 100);
	ApplyLayout(Module, Layout);
}

void MigrateFromLegacyConfigs();

void EnsureLegacyMigrated()
{
	MigrateFromLegacyConfigs();
}

void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	(void)pUserData;
	EnsureLegacyMigrated();
	EnsureRuntimeLayouts();

	char aLine[320];
	for(int Module = 0; Module < MODULE_COUNT; ++Module)
	{
		if(!IsPersistedModule((EModule)Module))
			continue;
		const SModuleLayout &Layout = gs_aRuntimeModuleLayouts[Module];
		str_format(
			aLine,
			sizeof(aLine),
			"hud_layout_set %s %.3f %.3f %d %d %d %u %d %d",
			Id((EModule)Module),
			Layout.m_X,
			Layout.m_Y,
			Layout.m_Scale,
			Layout.m_Mode,
			Layout.m_BackgroundEnabled ? 1 : 0,
			Layout.m_BackgroundColor,
			Layout.m_Enabled ? 1 : 0,
			Layout.m_Alpha);
		pConfigManager->WriteLine(aLine, ConfigDomain::TCLIENT);
	}
}

void MigrateFromLegacyConfigs()
{
	if(gs_LegacyMigrated)
		return;
	EnsureRuntimeLayouts();

	auto MigrateIfDefault = [](EModule Module, float X, float Y, int Scale) {
		if(HasRuntimeOverrideInternal(Module))
			return;
		SModuleLayout Layout = gs_aModuleLayouts[Module];
		Layout.m_X = X;
		Layout.m_Y = Y;
		Layout.m_Scale = std::clamp(Scale, 25, 300);
		ApplyLayout(Module, Layout);
	};

	MigrateIfDefault(MODULE_VOICE_TALKERS, (float)g_Config.m_BcHudVoiceHudX, (float)g_Config.m_BcHudVoiceHudY, g_Config.m_BcHudVoiceHudScale);
	MigrateIfDefault(MODULE_VOICE_STATUS, (float)g_Config.m_BcHudVoiceMuteIconsX, (float)g_Config.m_BcHudVoiceMuteIconsY, g_Config.m_BcHudVoiceMuteIconsScale);
	MigrateIfDefault(MODULE_CHAT, (float)g_Config.m_BcHudChatX, (float)g_Config.m_BcHudChatY, g_Config.m_BcHudChatScale);
	MigrateIfDefault(MODULE_VOTES, (float)g_Config.m_BcHudVotesX, (float)g_Config.m_BcHudVotesY, g_Config.m_BcHudVotesScale);

	if(!HasRuntimeOverrideInternal(MODULE_NOTIFY_LAST))
	{
		SModuleLayout Layout = gs_aModuleLayouts[MODULE_NOTIFY_LAST];
		Layout.m_X = (g_Config.m_TcNotifyWhenLastX / 100.0f) * CANVAS_WIDTH;
		Layout.m_Y = (g_Config.m_TcNotifyWhenLastY / 100.0f) * CANVAS_HEIGHT;
		ApplyLayout(MODULE_NOTIFY_LAST, Layout);
	}

	g_Config.m_BcHudVoiceHudX = (int)gs_aModuleLayouts[MODULE_VOICE_TALKERS].m_X;
	g_Config.m_BcHudVoiceHudY = (int)gs_aModuleLayouts[MODULE_VOICE_TALKERS].m_Y;
	g_Config.m_BcHudVoiceHudScale = gs_aModuleLayouts[MODULE_VOICE_TALKERS].m_Scale;
	g_Config.m_BcHudVoiceMuteIconsX = (int)gs_aModuleLayouts[MODULE_VOICE_STATUS].m_X;
	g_Config.m_BcHudVoiceMuteIconsY = (int)gs_aModuleLayouts[MODULE_VOICE_STATUS].m_Y;
	g_Config.m_BcHudVoiceMuteIconsScale = gs_aModuleLayouts[MODULE_VOICE_STATUS].m_Scale;
	g_Config.m_BcHudChatX = (int)gs_aModuleLayouts[MODULE_CHAT].m_X;
	g_Config.m_BcHudChatY = (int)gs_aModuleLayouts[MODULE_CHAT].m_Y;
	g_Config.m_BcHudChatScale = gs_aModuleLayouts[MODULE_CHAT].m_Scale;
	g_Config.m_BcHudVotesX = (int)gs_aModuleLayouts[MODULE_VOTES].m_X;
	g_Config.m_BcHudVotesY = (int)gs_aModuleLayouts[MODULE_VOTES].m_Y;
	g_Config.m_BcHudVotesScale = gs_aModuleLayouts[MODULE_VOTES].m_Scale;
	g_Config.m_TcNotifyWhenLastX = 20;
	g_Config.m_TcNotifyWhenLastY = 1;

	gs_LegacyMigrated = true;
}

} // namespace





bool IsEditorModule(EModule Module)
{
	switch(Module)
	{
	case MODULE_SCORE:
	case MODULE_SPECTATOR_COUNT:
	case MODULE_MOVEMENT_INFO:
	case MODULE_DUMMY_ACTIONS:
	case MODULE_CHAT:
	case MODULE_VOTES:
	case MODULE_LOCAL_TIME:
	case MODULE_FROZEN_HUD:
	case MODULE_NOTIFY_LAST:
	// bestclient
	case MODULE_VOICE_TALKERS:
	case MODULE_VOICE_STATUS:
	// bestclient
	case MODULE_EDGE_INFO:
	// bestclient
	case MODULE_FINISH_PREDICTION:
	case MODULE_KEYSTROKES_KEYBOARD:
	case MODULE_KEYSTROKES_MOUSE:
	// bestclient
		return true;
	default:
		return false;
	}
}

bool IsPersistedModule(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT && Module != MODULE_GAME_TIMER;
}

const char *Id(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT ? gs_apModuleIds[Module] : "unknown";
}

const char *Name(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT ? gs_apModuleNames[Module] : "HUD Module";
}

EModule ModuleFromId(const char *pId)
{
	if(!pId || pId[0] == '\0')
		return MODULE_COUNT;
	for(int i = 0; i < MODULE_COUNT; ++i)
	{
		if(str_comp(gs_apModuleIds[i], pId) == 0)
			return (EModule)i;
	}
	return MODULE_COUNT;
}

SModuleLayout Get(EModule Module, float HudWidth, float HudHeight)
{
	EnsureLegacyMigrated();
	return ResolveBaseLayout(Module, HudWidth, HudHeight);
}

SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight)
{
	SModuleLayout Layout = DynamicDefaultLayout(Module, HudWidth, HudHeight);
	if(!HasDynamicDefault(Module))
		Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
	return Layout;
}

bool HasRuntimeOverride(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT && HasRuntimeOverrideInternal(Module);
}

bool HasPositionOverride(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	EnsureRuntimeLayouts();
	const SModuleLayout &Layout = gs_aRuntimeModuleLayouts[Module];
	const SModuleLayout &Default = gs_aModuleLayouts[Module];
	return Layout.m_X != Default.m_X || Layout.m_Y != Default.m_Y || Layout.m_Mode != Default.m_Mode;
}

void SetPosition(EModule Module, float X, float Y)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_X = X;
	gs_aRuntimeModuleLayouts[Module].m_Y = Y;
}

void SetPosition(EModule Module, float X, float Y, EPositionMode PositionMode)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_X = X;
	gs_aRuntimeModuleLayouts[Module].m_Y = Y;
	gs_aRuntimeModuleLayouts[Module].m_Mode = PositionMode;
}

void SetScale(EModule Module, int Scale)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Scale = std::clamp(Scale, 25, 300);
	if(Module == MODULE_CHAT)
		g_Config.m_BcHudChatScale = gs_aRuntimeModuleLayouts[Module].m_Scale;
}

void SetEnabled(EModule Module, bool Enabled)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Enabled = Enabled;
}

void SetAlpha(EModule Module, int Alpha)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Alpha = std::clamp(Alpha, 0, 100);
}

bool IsEnabled(EModule Module)
{
	EnsureRuntimeLayouts();
	return gs_aRuntimeModuleLayouts[Module].m_Enabled;
}

int Alpha(EModule Module)
{
	EnsureRuntimeLayouts();
	return gs_aRuntimeModuleLayouts[Module].m_Alpha;
}

float AlphaFactor(EModule Module)
{
	return std::clamp(Alpha(Module) / 100.0f, 0.0f, 1.0f);
}

void ResetPosition(EModule Module)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_X = gs_aModuleLayouts[Module].m_X;
	gs_aRuntimeModuleLayouts[Module].m_Y = gs_aModuleLayouts[Module].m_Y;
	gs_aRuntimeModuleLayouts[Module].m_Mode = gs_aModuleLayouts[Module].m_Mode;
}

void ResetScale(EModule Module)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Scale = gs_aModuleLayouts[Module].m_Scale;
	if(Module == MODULE_CHAT)
		g_Config.m_BcHudChatScale = gs_aRuntimeModuleLayouts[Module].m_Scale;
}

void ResetSettings(EModule Module)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module] = gs_aModuleLayouts[Module];
	if(Module == MODULE_CHAT)
		g_Config.m_BcHudChatScale = gs_aRuntimeModuleLayouts[Module].m_Scale;
}

void ResetEditableModules()
{
	for(int Module = 0; Module < MODULE_COUNT; ++Module)
	{
		if(IsEditorModule((EModule)Module))
			ResetSettings((EModule)Module);
	}
}

SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight)
{
	SModuleRect Result = Rect;
	Result.m_X = std::clamp(Result.m_X, 0.0f, std::max(0.0f, HudWidth - Result.m_W));
	Result.m_Y = std::clamp(Result.m_Y, 0.0f, std::max(0.0f, HudHeight - Result.m_H));
	return Result;
}

float CanvasXToHud(float CanvasX, float HudWidth)
{
	return CanvasX * (HudWidth / CANVAS_WIDTH);
}

int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight)
{
	int Corners = DefaultCorners;
	const float Eps = 0.01f;
	if(RectW <= 0.0f || RectH <= 0.0f)
		return Corners;
	if(RectX <= Eps)
		Corners &= ~IGraphics::CORNER_L;
	if(RectX + RectW >= CanvasWidth - Eps)
		Corners &= ~IGraphics::CORNER_R;
	if(RectY <= Eps)
		Corners &= ~IGraphics::CORNER_T;
	if(RectY + RectH >= CanvasHeight - Eps)
		Corners &= ~IGraphics::CORNER_B;
	return Corners;
}

void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager)
{
	if(!gs_ConsoleCommandRegistered && pConsole)
	{
		pConsole->Register(
			"hud_layout_set",
			// bestclient
			"s[module] f[x] f[y] i[scale] i[mode] i[background_enabled] s[background_color] ?i[enabled] ?i[alpha]",
			// bestclient
			CFGFLAG_CLIENT,
			ConHudLayoutSet,
			nullptr,
			"Set HUD module layout entry");
		gs_ConsoleCommandRegistered = true;
	}

	if(!gs_ConfigCallbackRegistered && pConfigManager)
	{
		pConfigManager->RegisterCallback(ConfigSaveCallback, nullptr, ConfigDomain::TCLIENT);
		gs_ConfigCallbackRegistered = true;
	}
}

} // namespace HudLayout
