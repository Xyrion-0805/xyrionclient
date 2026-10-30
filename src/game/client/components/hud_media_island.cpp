// QmClient HUD 灵动岛原版渲染与动画；来源见 docs/xyrion/QMCLIENT_PORT.md。
#include "hud.h"
#include <base/color.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/shared/config.h>
#include <engine/font_icons.h>
#include <engine/demo.h>
#include <engine/textrender.h>
#include <generated/protocol7.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/qm_icon_font_render.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/localization.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <game/client/prediction/entities/character.h>

bool CHud::ReferencesMediaIslandTexture(IGraphics::CTextureHandle Texture) const
{
	if(!Texture.IsValid()) return false;
	const auto &State = m_MediaIslandAnimState;
	return (State.m_CurrentTrack.m_HasCover && State.m_CurrentTrack.m_Cover.Id() == Texture.Id()) ||
		(State.m_OutgoingTrack.m_HasCover && State.m_OutgoingTrack.m_Cover.Id() == Texture.Id());
}

void CHud::ReleaseMediaIslandTracks()
{
	m_MediaIslandAnimState.Reset();
	m_MediaIslandFrameCache.Reset();
	m_MediaIslandLastVisibleRectValid = false;
}

vec2 CHud::GetMediaIslandPushOffset(const CUIRect &Rect, float CanvasWidth, float CanvasHeight, float Padding) const
{
	if(!g_Config.m_QmHudIslandEnable || !m_MediaIslandLastVisibleRectValid || !g_Config.m_ClShowhud || m_Width <= 0 || m_Height <= 0)
		return vec2(0, 0);
	CUIRect Island = m_MediaIslandLastVisibleRect;
	Island.x *= CanvasWidth / m_Width;
	Island.w *= CanvasWidth / m_Width;
	Island.y *= CanvasHeight / m_Height;
	Island.h *= CanvasHeight / m_Height;
	if(Rect.x + Rect.w <= Island.x - Padding || Rect.x >= Island.x + Island.w + Padding || Rect.y + Rect.h <= Island.y - Padding || Rect.y >= Island.y + Island.h + Padding)
		return vec2(0, 0);
	return vec2(0, std::max(0.0f, Island.y + Island.h + Padding - Rect.y));
}
namespace
{
	constexpr float MEDIA_ISLAND_SATELLITE_RING_RADIUS_SCALE = 0.75f;
	constexpr float MEDIA_ISLAND_SATELLITE_RING_THICKNESS_SCALE = 0.15f;
	constexpr float MEDIA_ISLAND_OUTER_SHADOW_PIXELS = 5.0f;
	constexpr float MEDIA_ISLAND_OUTER_SHADOW_OPACITY = 0.35f;

	// island 背景模糊按 1/4 分辨率准备渲染目标（与 UI 半透明面板的模糊分辨率一致）
	int MediaIslandBlurTargetDimension(int ScreenDimension)
	{
		return ScreenDimension > 0 ? (ScreenDimension + 3) / 4 : 0;
	}

;

	uint64_t HudMediaIslandNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_media_island"));
		return (s_BaseKey << 32) | static_cast<uint64_t>(str_quickhash(pScope));
	}

	void RelaxMediaIslandEntranceSprings(CUiV2AnimationRuntime &AnimRuntime)
	{
		// 隐藏即打断：入场弹簧目标回落到 0，以当前速度回落；重现时继承速度继续（可中断 + 速度继承）。
		QmHudMediaIslandResolveEntranceSprings(AnimRuntime, HudMediaIslandNodeKey("entrance_drop"), HudMediaIslandNodeKey("entrance_expand"), false);
	}

	uint64_t HudMediaIslandSatelliteNodeKey(EHudMediaIslandCountdownType Type, int Id)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_media_island_satellite_item"));
		return (s_BaseKey << 32) | (static_cast<uint64_t>(static_cast<int>(Type) & 0xff) << 24) | static_cast<uint64_t>(Id & 0x00ffffff);
	}

	uint64_t HudRecordingStatusNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_recording_status"));
		return (s_BaseKey << 32) | static_cast<uint64_t>(str_quickhash(pScope));
	}

	int GetDisplayedCheckpoint(CGameClient &GameClient)
	{
		if(GameClient.m_Snap.m_pGameInfoObj == nullptr)
			return 0;

		int ClientId = -1;
		if(GameClient.m_Snap.m_pLocalCharacter != nullptr && !GameClient.m_Snap.m_SpecInfo.m_Active &&
			!(GameClient.m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			ClientId = GameClient.m_Snap.m_LocalClientId;
		}
		else if(GameClient.m_Snap.m_SpecInfo.m_Active && GameClient.m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		{
			ClientId = GameClient.m_Snap.m_SpecInfo.m_SpectatorId;
		}

		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return 0;

		CCharacter *pCharacter = nullptr;
		if(ClientId == GameClient.m_Snap.m_LocalClientId && !GameClient.m_Snap.m_SpecInfo.m_Active)
			pCharacter = GameClient.m_PredictedWorld.GetCharacterById(ClientId);
		if(pCharacter == nullptr)
			pCharacter = GameClient.m_GameWorld.GetCharacterById(ClientId);
		if(pCharacter == nullptr)
			pCharacter = GameClient.m_PredictedWorld.GetCharacterById(ClientId);

		if(pCharacter != nullptr && pCharacter->m_TeleCheckpoint > 0)
			return pCharacter->m_TeleCheckpoint;

		return 0;
	}

	struct SSwapCountdownInfo
	{
		ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		char m_aText[128] = {0};
		char m_aRequestText[128] = {0};
		int m_Dummy = 0;
		int m_InstanceId = 0;
		int m_StartTick = 0;
		bool m_Outgoing = false;
		SHudMediaIslandSwapLifecycle m_Lifecycle{};
	};

	struct SSwapCountdownList
	{
		std::array<SSwapCountdownInfo, NUM_DUMMIES *(MAX_CLIENTS + 1)> m_aInfos{};
		int m_Count = 0;
	};

	struct SHudFrozenTeamInfo
	{
		bool m_Available = false;
		int m_NumInTeam = 0;
		int m_NumFrozen = 0;
		int m_LocalTeamId = 0;
	};

	struct SHudFrozenHudRect
	{
		bool m_Visible = false;
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	struct SHudGameTimerInfo
	{
		bool m_Visible = false;
		bool m_IsCritical = false;
		float m_Alpha = 1.0f;
		float m_FontSize = 10.0f;
		float m_X = 0.0f;
		float m_Y = 2.0f;
		float m_W = 0.0f;
		float m_Left = 0.0f;
		char m_aText[32] = {};
	};

	struct SHudTopTimerCapsuleInfo
	{
		bool m_Visible = false;
		bool m_IsCritical = false;
		float m_Alpha = 1.0f;
		float m_FontSize = 10.0f;
		float m_BoxX = 0.0f;
		float m_BoxY = 1.0f;
		float m_BoxW = 0.0f;
		float m_BoxH = 16.0f;
		float m_TextX = 0.0f;
		float m_TextY = 0.0f;
		char m_aText[32] = {};
	};

	SHudFrozenTeamInfo BuildHudFrozenTeamInfo(const CGameClient &GameClient)
	{
		SHudFrozenTeamInfo Result;
		if(!GameClient.m_GameInfo.m_EntitiesDDRace)
			return Result;

		Result.m_Available = true;
		if(GameClient.m_Snap.m_LocalClientId >= 0 && GameClient.m_Snap.m_SpecInfo.m_SpectatorId >= 0)
		{
			if(GameClient.m_Snap.m_SpecInfo.m_Active == 1 && GameClient.m_Snap.m_SpecInfo.m_SpectatorId != -1)
				Result.m_LocalTeamId = GameClient.m_Teams.Team(GameClient.m_Snap.m_SpecInfo.m_SpectatorId);
			else
				Result.m_LocalTeamId = GameClient.m_Teams.Team(GameClient.m_Snap.m_LocalClientId);
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameClient.m_Snap.m_apPlayerInfos[i])
				continue;

			if(GameClient.m_Teams.Team(i) == Result.m_LocalTeamId)
			{
				Result.m_NumInTeam++;
				if((GameClient.m_aClients[i].m_FreezeEnd != 0 || GameClient.m_aClients[i].m_DeepFrozen))
					Result.m_NumFrozen++;
			}
		}

		return Result;
	}

	bool BuildHudFrozenSummaryText(const SHudFrozenTeamInfo &FrozenInfo, char *pBuf, size_t BufSize)
	{
		pBuf[0] = '\0';
		if(!FrozenInfo.m_Available || g_Config.m_QmShowFrozenText <= 0)
			return false;

		if(g_Config.m_QmShowFrozenText == 1)
			str_format(pBuf, BufSize, "%d/%d", FrozenInfo.m_NumInTeam - FrozenInfo.m_NumFrozen, FrozenInfo.m_NumInTeam);
		else if(g_Config.m_QmShowFrozenText == 2)
			str_format(pBuf, BufSize, "%d/%d", FrozenInfo.m_NumFrozen, FrozenInfo.m_NumInTeam);
		else
			return false;

		return true;
	}

	bool BuildHudTeamText(const CGameClient &GameClient, char *pBuf, size_t BufSize)
	{
		pBuf[0] = '\0';
		int ClientId = GameClient.m_Snap.m_LocalClientId;
		if(GameClient.m_Snap.m_SpecInfo.m_Active)
		{
			if(GameClient.m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
				return false;
			ClientId = GameClient.m_Snap.m_SpecInfo.m_SpectatorId;
		}
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || GameClient.m_Snap.m_apPlayerInfos[ClientId] == nullptr)
			return false;

		const int Team = GameClient.m_Teams.Team(ClientId);
		if(!QmHudMediaIslandShouldShowTeam(g_Config.m_QmHudIslandShowTeam, GameClient.m_GameInfo.m_EntitiesDDRace, Team))
			return false;

		str_format(pBuf, BufSize, Localize("Team %d"), Team);
		return pBuf[0] != '\0';
	}

	SHudGameTimerInfo BuildHudGameTimerInfo(const CGameClient &GameClient, const IClient &Client, ITextRender *pTextRender, float HudWidth)
	{
		SHudGameTimerInfo Result;
		const CNetObj_GameInfo *pGameInfoObj = GameClient.m_Snap.m_pGameInfoObj;
		if(pGameInfoObj == nullptr || (pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH) != 0)
			return Result;

		int Time = 0;
		if(pGameInfoObj->m_TimeLimit && pGameInfoObj->m_WarmupTimer <= 0)
		{
			Time = pGameInfoObj->m_TimeLimit * 60 - ((Client.GameTick(g_Config.m_ClDummy) - pGameInfoObj->m_RoundStartTick) / Client.GameTickSpeed());

			if(pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
				Time = 0;
		}
		else if(pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
		{
			// The Warmup timer is negative in this case to make sure that incompatible clients will not see a warmup timer
			Time = (Client.GameTick(g_Config.m_ClDummy) + pGameInfoObj->m_WarmupTimer) / Client.GameTickSpeed();
		}
		else
		{
			Time = (Client.GameTick(g_Config.m_ClDummy) - pGameInfoObj->m_RoundStartTick) / Client.GameTickSpeed();
		}

		str_time((int64_t)Time * 100, ETimeFormat::DAYS, Result.m_aText, sizeof(Result.m_aText));

		static float s_TextWidthM = pTextRender->TextWidth(Result.m_FontSize, "00:00", -1, -1.0f);
		static float s_TextWidthH = pTextRender->TextWidth(Result.m_FontSize, "00:00:00", -1, -1.0f);
		static float s_TextWidth0D = pTextRender->TextWidth(Result.m_FontSize, "0d 00:00:00", -1, -1.0f);
		static float s_TextWidth00D = pTextRender->TextWidth(Result.m_FontSize, "00d 00:00:00", -1, -1.0f);
		static float s_TextWidth000D = pTextRender->TextWidth(Result.m_FontSize, "000d 00:00:00", -1, -1.0f);

		Result.m_W = Time >= 3600 * 24 * 100 ? s_TextWidth000D :
						       (Time >= 3600 * 24 * 10 ? s_TextWidth00D :
										 (Time >= 3600 * 24 ? s_TextWidth0D :
												      (Time >= 3600 ? s_TextWidthH : s_TextWidthM)));

		Result.m_X = HudWidth * 0.5f - Result.m_W * 0.5f;
		Result.m_Left = Result.m_X;
		Result.m_IsCritical = pGameInfoObj->m_TimeLimit && Time <= 60 && pGameInfoObj->m_WarmupTimer <= 0;
		Result.m_Alpha = Time <= 10 && (2 * time_get() / time_freq()) % 2 ? 0.5f : 1.0f;
		Result.m_Visible = true;
		return Result;
	}

	SHudTopTimerCapsuleInfo BuildHudTopTimerCapsuleInfo(const SHudGameTimerInfo &TimerInfo)
	{
		SHudTopTimerCapsuleInfo Result;
		if(!TimerInfo.m_Visible)
			return Result;

		constexpr float BoxY = 1.0f;
		constexpr float BoxH = QmHudMediaIslandScaled(16.0f);
		constexpr float PaddingX = QmHudMediaIslandScaled(4.0f);
		const float TimerTextWidth = TimerInfo.m_W * QmHudMediaIslandDesignScale;

		Result.m_Visible = true;
		Result.m_IsCritical = TimerInfo.m_IsCritical;
		Result.m_Alpha = TimerInfo.m_Alpha;
		Result.m_FontSize = QmHudMediaIslandScaled(TimerInfo.m_FontSize);
		Result.m_BoxY = BoxY;
		Result.m_BoxH = BoxH;
		Result.m_BoxW = std::round(TimerTextWidth + PaddingX * 2.0f);
		Result.m_BoxX = std::round(TimerInfo.m_X + TimerInfo.m_W * 0.5f - Result.m_BoxW * 0.5f);
		Result.m_TextX = std::round(Result.m_BoxX + (Result.m_BoxW - TimerTextWidth) * 0.5f);
		Result.m_TextY = std::round(BoxY + (BoxH - Result.m_FontSize) * 0.5f - QmHudMediaIslandScaled(0.5f));
		str_copy(Result.m_aText, TimerInfo.m_aText, sizeof(Result.m_aText));
		return Result;
	}

	bool BuildHudRecordingStatusText(const CGameClient &GameClient, char *pBuf, size_t BufSize)
	{
		pBuf[0] = '\0';

		const auto &&AppendRecorderInfo = [&](int Recorder, const char *pName) {
			if(GameClient.DemoRecorder(Recorder)->IsRecording())
			{
				char aTime[32];
				str_time((int64_t)GameClient.DemoRecorder(Recorder)->Length() * 100, ETimeFormat::HOURS, aTime, sizeof(aTime));
				if(pBuf[0] != '\0')
					str_append(pBuf, "  ", BufSize);
				str_append(pBuf, pName, BufSize);
				str_append(pBuf, " ", BufSize);
				str_append(pBuf, aTime, BufSize);
			}
		};

		AppendRecorderInfo(RECORDER_MANUAL, Localize("Manual"));
		AppendRecorderInfo(RECORDER_RACE, Localize("Race"));
		AppendRecorderInfo(RECORDER_AUTO, Localize("Auto"));
		AppendRecorderInfo(RECORDER_REPLAYS, Localize("Replay"));
		return pBuf[0] != '\0';
	}

	bool ShouldRenderHudLocalTime(const CGameClient &GameClient)
	{
		return g_Config.m_QmHudIslandShowLocalTime || GameClient.m_Scoreboard.IsActive() || false;
	}

	void DrawSmoothRoundedRect(IGraphics *pGraphics, float x, float y, float w, float h, float r, ColorRGBA Color, int Corners = IGraphics::CORNER_ALL)
	{
		if(pGraphics == nullptr || w <= 0.0f || h <= 0.0f)
			return;

		r = std::clamp(r, 0.0f, std::min(w, h) * 0.5f);

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);

		if(Corners == 0 || r <= 0.0f)
		{
			IGraphics::CQuadItem QuadItem(x, y, w, h);
			pGraphics->QuadsDrawTL(&QuadItem, 1);
			pGraphics->QuadsEnd();
			return;
		}

		constexpr int NumSegments = 20;
		constexpr float Pi = 3.14159265359f;
		const float SegmentAngle = Pi / 2.0f / NumSegments;
		std::array<IGraphics::CFreeformItem, NumSegments * 2> aFreeform;
		int NumFreeformItems = 0;

		for(int i = 0; i < NumSegments; i += 2)
		{
			const float A1 = i * SegmentAngle;
			const float A2 = (i + 1) * SegmentAngle;
			const float A3 = (i + 2) * SegmentAngle;
			const float Ca1 = std::cos(A1);
			const float Ca2 = std::cos(A2);
			const float Ca3 = std::cos(A3);
			const float Sa1 = std::sin(A1);
			const float Sa2 = std::sin(A2);
			const float Sa3 = std::sin(A3);

			if(Corners & IGraphics::CORNER_TL)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + r, y + r,
					x + (1.0f - Ca1) * r, y + (1.0f - Sa1) * r,
					x + (1.0f - Ca3) * r, y + (1.0f - Sa3) * r,
					x + (1.0f - Ca2) * r, y + (1.0f - Sa2) * r);

			if(Corners & IGraphics::CORNER_TR)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + w - r, y + r,
					x + w - r + Ca1 * r, y + (1.0f - Sa1) * r,
					x + w - r + Ca3 * r, y + (1.0f - Sa3) * r,
					x + w - r + Ca2 * r, y + (1.0f - Sa2) * r);

			if(Corners & IGraphics::CORNER_BL)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + r, y + h - r,
					x + (1.0f - Ca1) * r, y + h - r + Sa1 * r,
					x + (1.0f - Ca3) * r, y + h - r + Sa3 * r,
					x + (1.0f - Ca2) * r, y + h - r + Sa2 * r);

			if(Corners & IGraphics::CORNER_BR)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + w - r, y + h - r,
					x + w - r + Ca1 * r, y + h - r + Sa1 * r,
					x + w - r + Ca3 * r, y + h - r + Sa3 * r,
					x + w - r + Ca2 * r, y + h - r + Sa2 * r);
		}

		if(NumFreeformItems > 0)
			pGraphics->QuadsDrawFreeform(aFreeform.data(), NumFreeformItems);

		std::array<IGraphics::CQuadItem, 9> aQuads;
		int NumQuadItems = 0;
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + r, y + r, w - r * 2.0f, h - r * 2.0f);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + r, y, w - r * 2.0f, r);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + r, y + h - r, w - r * 2.0f, r);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x, y + r, r, h - r * 2.0f);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + w - r, y + r, r, h - r * 2.0f);

		if(!(Corners & IGraphics::CORNER_TL))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x, y, r, r);
		if(!(Corners & IGraphics::CORNER_TR))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + w, y, -r, r);
		if(!(Corners & IGraphics::CORNER_BL))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x, y + h, r, -r);
		if(!(Corners & IGraphics::CORNER_BR))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + w, y + h, -r, -r);

		pGraphics->QuadsDrawTL(aQuads.data(), NumQuadItems);
		pGraphics->QuadsEnd();
	}

	void DrawSmoothCircle(IGraphics *pGraphics, vec2 Center, float Radius, ColorRGBA Color)
	{
		if(pGraphics == nullptr || Radius <= 0.0f)
			return;

		constexpr int NumSegments = 48;
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);
		pGraphics->DrawCircle(Center.x, Center.y, Radius, NumSegments);
		pGraphics->QuadsEnd();
	}

	void DrawMediaIslandArcGeometry(IGraphics *pGraphics, vec2 Center, float Radius, float Thickness, float Progress, ColorRGBA Color)
	{
		Progress = std::clamp(Progress, 0.0f, 1.0f);
		if(pGraphics == nullptr || Radius <= 0.0f || Thickness <= 0.0f || Progress <= 0.0f)
			return;

		constexpr int MaxSegments = 64;
		constexpr float Pi = 3.14159265359f;
		const int NumSegments = std::max(1, (int)std::ceil(MaxSegments * Progress));
		const float OuterRadius = Radius + Thickness * 0.5f;
		const float InnerRadius = std::max(0.0f, Radius - Thickness * 0.5f);
		const float Sweep = 2.0f * Pi * Progress;
		std::array<IGraphics::CFreeformItem, MaxSegments> aSegments;
		for(int i = 0; i < NumSegments; ++i)
		{
			const float Angle0 = Sweep * i / NumSegments;
			const float Angle1 = Sweep * (i + 1) / NumSegments;
			const vec2 Direction0(std::sin(Angle0), -std::cos(Angle0));
			const vec2 Direction1(std::sin(Angle1), -std::cos(Angle1));
			aSegments[i] = IGraphics::CFreeformItem(
				Center + Direction0 * OuterRadius,
				Center + Direction1 * OuterRadius,
				Center + Direction1 * InnerRadius,
				Center + Direction0 * InnerRadius);
		}

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);
		pGraphics->QuadsDrawFreeform(aSegments.data(), NumSegments);
		pGraphics->QuadsEnd();
	}

	ColorRGBA MediaIslandCountdownColor(EHudMediaIslandCountdownType Type)
	{
		switch(Type)
		{
		case EHudMediaIslandCountdownType::SWAP: return ColorRGBA(0.10f, 0.90f, 1.0f, 1.0f);
		case EHudMediaIslandCountdownType::SWITCH: return ColorRGBA(1.0f, 0.55f, 0.10f, 1.0f);
		case EHudMediaIslandCountdownType::MUTE: return ColorRGBA(1.0f, 0.20f, 0.24f, 1.0f);
		}
		return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}

	void DrawMediaIslandOuterShadowFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)
	{
		if(pGraphics == nullptr || State.m_OuterShadowSize <= 0.0f || State.m_OuterShadowOpacity <= 0.0f)
			return;

		constexpr int LayerCount = 4;
		const ColorRGBA LayerColor(0.0f, 0.0f, 0.0f, std::clamp(State.m_OuterShadowOpacity, 0.0f, 1.0f) / LayerCount);
		for(int Layer = LayerCount; Layer >= 1; --Layer)
		{
			const float Expansion = State.m_OuterShadowSize * Layer / LayerCount;
			DrawSmoothRoundedRect(
				pGraphics,
				State.m_MainRect.x - Expansion,
				State.m_MainRect.y - Expansion,
				State.m_MainRect.w + Expansion * 2.0f,
				State.m_MainRect.h + Expansion * 2.0f,
				State.m_MainRadius + Expansion,
				LayerColor,
				State.m_MainCorners);
			for(int i = 0; i < State.m_ItemCount; ++i)
			{
				const SHudMediaIslandSdfItem &Item = State.m_Items[i];
				const float Radius = std::max(0.0f, std::min(Item.m_Radii.x, Item.m_Radii.y));
				DrawSmoothRoundedRect(
					pGraphics,
					Item.m_Center.x - Item.m_Radii.x - Expansion,
					Item.m_Center.y - Item.m_Radii.y - Expansion,
					Item.m_Radii.x * 2.0f + Expansion * 2.0f,
					Item.m_Radii.y * 2.0f + Expansion * 2.0f,
					Radius + Expansion,
					LayerColor);
			}
			if(State.m_HasRightCapsule && State.m_RightCapsule.m_Rect.w > 0.0f && State.m_RightCapsule.m_Rect.h > 0.0f)
			{
				const CUIRect &Rect = State.m_RightCapsule.m_Rect;
				DrawSmoothRoundedRect(
					pGraphics,
					Rect.x - Expansion,
					Rect.y - Expansion,
					Rect.w + Expansion * 2.0f,
					Rect.h + Expansion * 2.0f,
					State.m_RightCapsule.m_Radius + Expansion,
					LayerColor,
					IGraphics::CORNER_ALL);
			}
		}
	}

	void DrawMediaIslandGeometryFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)
	{
		if(pGraphics == nullptr)
			return;
		DrawMediaIslandOuterShadowFallback(pGraphics, State);
		DrawSmoothRoundedRect(pGraphics, State.m_MainRect.x, State.m_MainRect.y, State.m_MainRect.w, State.m_MainRect.h, State.m_MainRadius, State.m_BackgroundColor, State.m_MainCorners);
		for(int i = 0; i < State.m_ItemCount; ++i)
		{
			const SHudMediaIslandSdfItem &Item = State.m_Items[i];
			const float Radius = std::max(0.0f, std::min(Item.m_Radii.x, Item.m_Radii.y));
			DrawSmoothRoundedRect(
				pGraphics,
				Item.m_Center.x - Item.m_Radii.x,
				Item.m_Center.y - Item.m_Radii.y,
				Item.m_Radii.x * 2.0f,
				Item.m_Radii.y * 2.0f,
				Radius,
				State.m_BackgroundColor);
			if(Item.m_ContentAlpha > 0.001f)
			{
				const float RingRadius = State.m_RingRadius * Item.m_ContentScale;
				const float RingThickness = State.m_RingThickness * Item.m_ContentScale;
				DrawMediaIslandArcGeometry(pGraphics, Item.m_Center, RingRadius, RingThickness, 1.0f, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * 0.18f * Item.m_ContentAlpha));
				DrawMediaIslandArcGeometry(pGraphics, Item.m_Center, RingRadius, RingThickness, Item.m_CountdownProgress, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * Item.m_ContentAlpha));
			}
		}
		if(State.m_HasRightCapsule && State.m_RightCapsule.m_Rect.w > 0.0f && State.m_RightCapsule.m_Rect.h > 0.0f)
		{
			const CUIRect &Rect = State.m_RightCapsule.m_Rect;
			DrawSmoothRoundedRect(pGraphics, Rect.x, Rect.y, Rect.w, Rect.h, State.m_RightCapsule.m_Radius, State.m_BackgroundColor, IGraphics::CORNER_ALL);
		}
	}

	// 两处录制状态共用同一个 SDF 圆点；不支持 SDF 时保留几何圆。
	void DrawHudRecordingStatusDot(IGraphics *pGraphics, vec2 Center, float DotSize, float Alpha, float ScreenPixelSize)
	{
		if(pGraphics == nullptr || DotSize <= 0.0f || Alpha <= 0.0f)
			return;

		if(!pGraphics->HasMediaIslandSdf())
		{
			DrawSmoothCircle(pGraphics, Center, DotSize * 0.5f, ColorRGBA(1.0f, 0.15f, 0.15f, Alpha));
			return;
		}

		const SHudMediaIslandSdfRenderState State = QmHudRecordingDotSdfState(Center, DotSize, Alpha, ScreenPixelSize);
		IGraphics::SMediaIslandSdfParams GpuSdfParams;
		if(QmHudMediaIslandBuildGpuSdfParams(State, GpuSdfParams))
		{
			pGraphics->RenderMediaIslandSdf(GpuSdfParams);
			return;
		}
		DrawSmoothCircle(pGraphics, Center, DotSize * 0.5f, ColorRGBA(1.0f, 0.15f, 0.15f, Alpha));
	}

	EQmIcon MediaIslandCountdownIcon(EHudMediaIslandCountdownType Type, bool Completed = false, bool SwapOutgoing = false)
	{
		if(Completed)
			return EQmIcon::CHECK;
		switch(Type)
		{
		case EHudMediaIslandCountdownType::SWAP: return SwapOutgoing ? EQmIcon::ARROW_RIGHT : EQmIcon::ARROW_LEFT;
		case EHudMediaIslandCountdownType::SWITCH: return EQmIcon::SWAP;
		case EHudMediaIslandCountdownType::MUTE: return EQmIcon::SPEAKER_SLASH;
		}
		return EQmIcon::COUNT;
	}

	const char *MediaIslandCountdownFontIcon(EQmIcon Icon)
	{
		switch(Icon)
		{
		case EQmIcon::CHECK: return "\xEE\x86\x82";
		case EQmIcon::ARROW_LEFT: return "\xEE\x81\x98";
		case EQmIcon::ARROW_RIGHT: return "\xEE\x81\xAC";
		case EQmIcon::SWAP: return "\xEE\xA0\xBC";
		case EQmIcon::SPEAKER_SLASH: return "\xEE\x91\x9A";
		default: return nullptr;
		}
	}

	void DrawTexturedQuad(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, vec2 Center, float Radius, float Alpha = 1.0f)
	{
		if(pGraphics == nullptr || !Texture.IsValid() || Radius <= 0.0f)
			return;

		pGraphics->WrapClamp();
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		const IGraphics::CQuadItem CoverQuad(Center.x - Radius, Center.y - Radius, Radius * 2.0f, Radius * 2.0f);
		pGraphics->QuadsDrawTL(&CoverQuad, 1);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
		pGraphics->TextureClear();
	}

	bool BuildSwapCountdownInfo(const CGameClient &GameClient, IClient &Client, int Dummy, const SSwapCountdownState &State, SSwapCountdownInfo &Out)
	{
		if(Dummy < 0 || Dummy >= NUM_DUMMIES)
			return false;

		const int StartTick = State.m_StartTick;
		if(StartTick <= 0)
			return false;

		const int TickSpeed = Client.GameTickSpeed();
		const int CurTick = Client.GameTick(Dummy);
		const SHudMediaIslandSwapLifecycle Lifecycle = QmHudMediaIslandSwapLifecycle(StartTick, CurTick, TickSpeed);
		if(!Lifecycle.m_Visible)
			return false;

		const char *pCounterpart = State.m_Counterpart.c_str();
		if(pCounterpart[0] == '\0')
			pCounterpart = "?";
		const int TargetClientId = GameClient.m_aLocalIds[Dummy];
		const char *pLocal = TargetClientId >= 0 && TargetClientId < MAX_CLIENTS && GameClient.m_aClients[TargetClientId].m_aName[0] != '\0' ?
					     GameClient.m_aClients[TargetClientId].m_aName :
					     (Dummy == 0 ? Client.PlayerName() : Client.DummyName());
		const int SecondsLeft = Lifecycle.m_SecondsLeft;
		Out.m_Dummy = Dummy;
		Out.m_InstanceId = State.m_InstanceId * NUM_DUMMIES + Dummy;
		Out.m_StartTick = StartTick;
		Out.m_Outgoing = State.m_Outgoing;
		Out.m_Lifecycle = Lifecycle;
		const char *pFrom = Out.m_Outgoing ? pLocal : pCounterpart;
		const char *pTo = Out.m_Outgoing ? pCounterpart : pLocal;
		if(!Out.m_Outgoing)
			str_format(Out.m_aRequestText, sizeof(Out.m_aRequestText), Localize("%s has requested to swap with %s"), pFrom, pTo);
		if(!Lifecycle.m_Completed)
		{
			str_format(Out.m_aText, sizeof(Out.m_aText), "%s->%s Swap:%d秒", pFrom, pTo, SecondsLeft);
			Out.m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			str_format(Out.m_aText, sizeof(Out.m_aText), "%s->%s 可交换!", pFrom, pTo);
			Out.m_TextColor = ColorRGBA(0.5f, 1.0f, 0.5f, 1.0f);
		}

		return true;
	}

	bool BuildSwapCountdownList(const CGameClient &GameClient, IClient &Client, SSwapCountdownList &Out)
	{
		Out.m_Count = 0;
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			if(!QmHudMediaIslandSwapVisibleForConnection(Dummy, g_Config.m_ClDummy))
				continue;
			for(const SSwapCountdownState &State : GameClient.m_Hud.MediaIslandSwaps(Dummy))
			{
				if(Out.m_Count < (int)Out.m_aInfos.size() && BuildSwapCountdownInfo(GameClient, Client, Dummy, State, Out.m_aInfos[Out.m_Count]))
					++Out.m_Count;
			}
		}
		return Out.m_Count > 0;
	}

	int GetMediaIslandSpectatorCount(const CGameClient &GameClient, const IClient &Client)
	{
		int Count = 0;
		if(Client.IsSixup())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(i == GameClient.m_aLocalIds[0] || (Client.DummyConnected() && i == GameClient.m_aLocalIds[1]))
					continue;

				if(Client.m_TranslationContext.m_aClients[i].m_PlayerFlags7 & protocol7::PLAYERFLAG_WATCHING)
					++Count;
			}
		}
		else if(const CNetObj_SpectatorCount *pSpectatorCount = GameClient.m_Snap.m_pSpectatorCount)
		{
			Count = std::max(pSpectatorCount->m_NumSpectators, 0);
		}

		return Count;
	}

}

void CHud::DestroyMediaIslandBlurTargets()
{
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurSource);
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurDownsample);
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurDownsampleTemporary);
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurDownsampleTarget);
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurTarget);
	m_MediaIslandBlurWidth = 0;
	m_MediaIslandBlurHeight = 0;
	m_MediaIslandBlurMode = -1;
	m_MediaIslandBlurReady = false;
	m_MediaIslandBlurLastAttemptFrame = 0;
	m_MediaIslandBlurAttemptInitialized = false;
}

IGraphics::CRenderTargetHandle CHud::MediaIslandBlurBackdrop()
{
	if(!QmHudMediaIslandShouldPrepareBackdropBlur(g_Config.m_QmHudIslandBgOpacity, g_Config.m_QmGaussianBlur != 0) || !Graphics()->HasMediaIslandSdf())
		return IGraphics::CRenderTargetHandle();
	if(!Graphics()->IsBackbufferCaptureSupported() || !Graphics()->IsRenderTargetGaussianBlurSupported())
	{
		if(m_MediaIslandBlurSource.IsValid() || m_MediaIslandBlurDownsample.IsValid() || m_MediaIslandBlurDownsampleTemporary.IsValid() || m_MediaIslandBlurDownsampleTarget.IsValid() || m_MediaIslandBlurTarget.IsValid())
			DestroyMediaIslandBlurTargets();
		return IGraphics::CRenderTargetHandle();
	}

	const int BlurWidth = MediaIslandBlurTargetDimension(Graphics()->ScreenWidth());
	const int BlurHeight = MediaIslandBlurTargetDimension(Graphics()->ScreenHeight());
	const int BlurMode = std::clamp(g_Config.m_QmBlurMode, 0, 2);
	if(BlurWidth <= 0 || BlurHeight <= 0)
	{
		m_MediaIslandBlurReady = false;
		m_MediaIslandBlurLastAttemptFrame = 0;
		m_MediaIslandBlurAttemptInitialized = false;
		return IGraphics::CRenderTargetHandle();
	}

	const bool SizeChanged = BlurWidth != m_MediaIslandBlurWidth || BlurHeight != m_MediaIslandBlurHeight;
	const int DualBlurWidth = std::max(1, (BlurWidth + 1) / 2);
	const int DualBlurHeight = std::max(1, (BlurHeight + 1) / 2);
	if(SizeChanged || !m_MediaIslandBlurSource.IsValid() || !m_MediaIslandBlurDownsample.IsValid() || !m_MediaIslandBlurDownsampleTemporary.IsValid() || !m_MediaIslandBlurDownsampleTarget.IsValid() || !m_MediaIslandBlurTarget.IsValid())
	{
		DestroyMediaIslandBlurTargets();
		m_MediaIslandBlurSource = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		m_MediaIslandBlurDownsample = Graphics()->CreateRenderTarget(DualBlurWidth, DualBlurHeight);
		m_MediaIslandBlurDownsampleTemporary = Graphics()->CreateRenderTarget(DualBlurWidth, DualBlurHeight);
		m_MediaIslandBlurDownsampleTarget = Graphics()->CreateRenderTarget(DualBlurWidth, DualBlurHeight);
		m_MediaIslandBlurTarget = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		if(!m_MediaIslandBlurSource.IsValid() || !m_MediaIslandBlurDownsample.IsValid() || !m_MediaIslandBlurDownsampleTemporary.IsValid() || !m_MediaIslandBlurDownsampleTarget.IsValid() || !m_MediaIslandBlurTarget.IsValid())
		{
			DestroyMediaIslandBlurTargets();
			return IGraphics::CRenderTargetHandle();
		}
		m_MediaIslandBlurWidth = BlurWidth;
		m_MediaIslandBlurHeight = BlurHeight;
		m_MediaIslandBlurMode = BlurMode;
	}

	const uint64_t CurrentFrame = static_cast<uint64_t>(time_get());
	const bool Refresh = QmHudMediaIslandShouldRefreshBackdropBlur(CurrentFrame, m_MediaIslandBlurLastAttemptFrame, m_MediaIslandBlurAttemptInitialized);
	if(Refresh)
	{
		m_MediaIslandBlurLastAttemptFrame = CurrentFrame;
		m_MediaIslandBlurAttemptInitialized = true;
		Ui()->PrepareGaussianBlur();
	}

	// QmClient：island 背景与 UI 半透明面板参数一致（同为 1/4 分辨率），UI 已准备同一次捕获/模糊
	// 结果时直接复用，避免重复模糊；UI 句柄销毁重建期间自然回退到下面的独立渲染目标管线。
	if(Ui()->GaussianBlurTargetReady())
		return Ui()->GaussianBlurTarget();

	// 独立管线：非刷新帧沿用上一帧结果，避免每帧重复捕获与模糊
	if(!Refresh)
		return m_MediaIslandBlurReady ? m_MediaIslandBlurTarget : IGraphics::CRenderTargetHandle();

	if(!Graphics()->CaptureBackbufferToRenderTarget(m_MediaIslandBlurSource))
	{
		m_MediaIslandBlurReady = false;
		return IGraphics::CRenderTargetHandle();
	}

	IGraphics::SGaussianBlurParams BlurParams;
	BlurParams.m_Radius = 5;
	BlurParams.m_Sigma = 2.5f;
	m_MediaIslandBlurReady = Graphics()->DualBlurRenderTarget(
		m_MediaIslandBlurSource,
		m_MediaIslandBlurDownsample,
		m_MediaIslandBlurDownsampleTemporary,
		m_MediaIslandBlurDownsampleTarget,
		m_MediaIslandBlurTarget,
		BlurParams);
	return m_MediaIslandBlurReady ? m_MediaIslandBlurTarget : IGraphics::CRenderTargetHandle();
}

void CHud::HandleSpamProtectionMessage(const char *pMessage)
{
	if(pMessage == nullptr || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	const char *pMainName = Client()->PlayerName();
	const int MainId = GameClient()->m_aLocalIds[0];
	if(MainId >= 0 && MainId < MAX_CLIENTS && GameClient()->m_aClients[MainId].m_aName[0] != '\0')
		pMainName = GameClient()->m_aClients[MainId].m_aName;

	const char *pDummyName = "";
	if(Client()->DummyConnected())
	{
		pDummyName = Client()->DummyName();
		const int DummyId = GameClient()->m_aLocalIds[1];
		if(DummyId >= 0 && DummyId < MAX_CLIENTS && GameClient()->m_aClients[DummyId].m_aName[0] != '\0')
			pDummyName = GameClient()->m_aClients[DummyId].m_aName;
	}

	int Seconds = 0;
	const EHudMediaIslandMuteMessage Message = QmHudParseSpamProtectionMute(pMessage, pMainName, pDummyName, Seconds);
	if(Message == EHudMediaIslandMuteMessage::NONE || Seconds <= 0)
		return;

	auto &MuteState = m_MediaIslandMuteState;
	const int64_t Now = time_get();
	const int64_t RemainingTicks = static_cast<int64_t>(Seconds) * time_freq();
	if(Message == EHudMediaIslandMuteMessage::SPAM_BROADCAST)
	{
		const bool Duplicate = MuteState.m_Confirmed && Now < MuteState.m_EndTick && std::abs(MuteState.m_DurationTicks - RemainingTicks) < time_freq();
		if(Duplicate)
			return;
		MuteState.m_Confirmed = true;
		MuteState.m_TriggerTick = Now;
		MuteState.m_DurationTicks = RemainingTicks;
		MuteState.m_EndTick = Now + RemainingTicks;
	}
	else if(MuteState.m_Confirmed && Now < MuteState.m_EndTick)
	{
		MuteState.m_EndTick = Now + RemainingTicks;
	}
}

void CHud::UpdateSwitchCountdownTracker()
{
	if(!g_Config.m_QmSwitchCountdown)
	{
		m_SwitchCountdownTracker.Reset();
		return;
	}

	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0 || Collision() == nullptr)
		return;

	const auto ClearTrackedSwitch = [&](int Team, int SwitchNumber) {
		m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] = 0;
		m_SwitchCountdownTracker.m_aaTouchTick[Team][SwitchNumber] = 0;
		m_SwitchCountdownTracker.m_aaClientId[Team][SwitchNumber] = -1;
		m_SwitchCountdownTracker.m_aaConnection[Team][SwitchNumber] = -1;
	};
	const auto UpdateSwitchCountdownFromClient = [&](int ClientId, int Connection, bool UsePredictedPos) {
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || Connection < 0 || Connection >= NUM_DUMMIES)
			return;
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			return;

		const vec2 Pos = UsePredictedPos ?
					 GameClient()->m_aClients[ClientId].m_Predicted.m_Pos :
					 vec2(GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);

		const int Team = GameClient()->m_Teams.Team(ClientId);
		if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
			return;

		const int MapIndex = Collision()->GetPureMapIndex(Pos);
		if(MapIndex < 0)
			return;

		const int SwitchType = Collision()->GetSwitchType(MapIndex);
		if(SwitchType != TILE_SWITCHTIMEDOPEN && SwitchType != TILE_SWITCHTIMEDCLOSE)
			return;

		const int SwitchNumber = Collision()->GetSwitchNumber(MapIndex);
		if(SwitchNumber <= 0 || SwitchNumber >= 256)
			return;

		const int CurTick = Client()->GameTick(Connection);
		const int Delay = Collision()->GetSwitchDelay(MapIndex);
		m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] = CurTick + 1 + Delay * TickSpeed;
		m_SwitchCountdownTracker.m_aaTouchTick[Team][SwitchNumber] = CurTick;
		m_SwitchCountdownTracker.m_aaClientId[Team][SwitchNumber] = ClientId;
		m_SwitchCountdownTracker.m_aaConnection[Team][SwitchNumber] = Connection;
	};

	UpdateSwitchCountdownFromClient(GameClient()->m_aLocalIds[0], 0, GameClient()->Predict());
	for(int Connection = 1; Connection < NUM_DUMMIES; ++Connection)
		if(Client()->DummyIsConnected(Connection))
			UpdateSwitchCountdownFromClient(GameClient()->m_aLocalIds[Connection], Connection, false);

	for(int Team = 0; Team < NUM_DDRACE_TEAMS; ++Team)
	{
		for(int SwitchNumber = 1; SwitchNumber < 256; ++SwitchNumber)
		{
			if(m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] <= 0)
				continue;
			const int Connection = m_SwitchCountdownTracker.m_aaConnection[Team][SwitchNumber];
			const int ClientId = m_SwitchCountdownTracker.m_aaClientId[Team][SwitchNumber];
			const bool ConnectionActive = Connection == 0 || (Connection > 0 && Connection < NUM_DUMMIES && Client()->DummyIsConnected(Connection));
			if(!ConnectionActive || ClientId < 0 || ClientId != GameClient()->m_aLocalIds[Connection] || m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] <= Client()->GameTick(Connection))
				ClearTrackedSwitch(Team, SwitchNumber);
		}
	}
}

bool CHud::HasActiveSwitchCountdown() const
{
	if(!g_Config.m_QmSwitchCountdown || !QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode))
		return false;
	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0)
		return false;

	const int Team = GameClient()->SwitchStateTeam();
	if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
		return false;

	const int CurTick = Client()->GameTick(g_Config.m_ClDummy);
	for(int i = 1; i < 256; ++i)
	{
		if(m_SwitchCountdownTracker.m_aaEndTick[Team][i] > CurTick && m_SwitchCountdownTracker.m_aaTouchTick[Team][i] > 0)
			return true;
	}

	return false;
}

void CHud::EnsureMediaIslandFrameCache() const
{
	const uint64_t CurrentFrame = static_cast<uint64_t>(time_get());
	SHudMediaIslandFrameCache &Cache = m_MediaIslandFrameCache;
	if(Cache.m_Valid && Cache.m_Frame == CurrentFrame)
		return;

	Cache.Reset();
	Cache.m_Frame = CurrentFrame;
	Cache.m_Valid = true;
	Cache.m_HasMediaState = g_Config.m_QmSmtcEnable && g_Config.m_QmSmtcShowHud && GameClient()->m_SystemMediaControls.GetStateSnapshot(Cache.m_MediaState);
	Cache.m_ShowLyrics = g_Config.m_QmLyricsInMediaIsland && GameClient()->m_SystemMediaControls.GetCurrentLyric(Cache.m_aLyrics, sizeof(Cache.m_aLyrics));
	Cache.m_LyricsActive = g_Config.m_QmLyricsInMediaIsland && GameClient()->m_SystemMediaControls.HasActiveLyrics();

	if(g_Config.m_QmHudIslandUseOriginalStyle)
		return;
	Cache.m_SpectatorCount = GetMediaIslandSpectatorCount(*GameClient(), *Client());

	if(m_MediaIslandAnimState.HasVisibleSatellite())
	{
		Cache.m_HasVisible = true;
		return;
	}
	if(m_MediaIslandMuteState.m_Confirmed && time_get() < m_MediaIslandMuteState.m_EndTick)
	{
		Cache.m_HasVisible = true;
		return;
	}
	if(Cache.m_SpectatorCount > 0)
	{
		Cache.m_HasVisible = true;
		return;
	}

	SSwapCountdownList SwapList;
	if(BuildSwapCountdownList(*GameClient(), *Client(), SwapList))
	{
		Cache.m_HasVisible = true;
		return;
	}

	if(HasActiveSwitchCountdown())
	{
		Cache.m_HasVisible = true;
		return;
	}

	if(ShouldRenderHudLocalTime(*GameClient()))
	{
		Cache.m_HasVisible = true;
		return;
	}
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
	char aFrozenSummaryBuf[64];
	if(BuildHudFrozenSummaryText(FrozenInfo, aFrozenSummaryBuf, sizeof(aFrozenSummaryBuf)))
	{
		Cache.m_HasVisible = true;
		return;
	}

	char aTeamBuf[32];
	if(BuildHudTeamText(*GameClient(), aTeamBuf, sizeof(aTeamBuf)))
	{
		Cache.m_HasVisible = true;
		return;
	}

	if(g_Config.m_ClShowhudTimer)
	{
		const SHudGameTimerInfo TimerInfo = BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width);
		if(TimerInfo.m_Visible)
		{
			Cache.m_HasVisible = true;
			return;
		}
	}

	Cache.m_HasVisible = Cache.m_ShowLyrics || Cache.m_LyricsActive || Cache.m_HasMediaState;
}

bool CHud::HasVisibleMediaIsland() const
{
	if(!g_Config.m_QmHudIslandEnable || g_Config.m_QmHudIslandUseOriginalStyle)
		return false;
	EnsureMediaIslandFrameCache();
	return m_MediaIslandFrameCache.m_HasVisible;
}

void CHud::RenderMediaIsland()
{
	if(!HasVisibleMediaIsland())
	{
		RelaxMediaIslandEntranceSprings(GameClient()->UiRuntimeV2()->AnimRuntime());
		m_MediaIslandAnimState.Reset();
		m_MediaIslandLastVisibleRectValid = false;
		if(m_MediaIslandBlurSource.IsValid()) DestroyMediaIslandBlurTargets();
		return;
	}
	EnsureMediaIslandFrameCache();
	auto &AnimState = m_MediaIslandAnimState;
	const int64_t Now = time_get();
	const CSystemMediaControls::SState &MediaState = m_MediaIslandFrameCache.m_MediaState;
	const bool HasMediaState = m_MediaIslandFrameCache.m_HasMediaState;
	const bool LyricsActive = m_MediaIslandFrameCache.m_LyricsActive;
	const bool AlwaysShowTrackDetails = g_Config.m_QmHudIslandAlwaysShowTrackDetails != 0;
	if(!HasMediaState)
	{
		AnimState.m_WaveformWasPlaying = false;
		AnimState.m_WaveformSettling = false;
		AnimState.m_WaveformSettleStartTick = 0;
		AnimState.m_WaveformSettleSampleTime = 0.0f;
	}
	SSwapCountdownList SwapList;
	BuildSwapCountdownList(*GameClient(), *Client(), SwapList);
	std::array<const SSwapCountdownInfo *, NUM_DUMMIES * MAX_CLIENTS> aIncomingSwapInfos{};
	int IncomingSwapCount = 0;
	for(int i = 0; i < SwapList.m_Count && IncomingSwapCount < (int)aIncomingSwapInfos.size(); ++i)
	{
		if(!SwapList.m_aInfos[i].m_Outgoing)
			aIncomingSwapInfos[IncomingSwapCount++] = &SwapList.m_aInfos[i];
	}

	std::array<SHudMediaIslandCountdownInput, SHudMediaIslandAnimState::SATELLITE_LIVE_MAX_ITEMS> aCountdownInputs{};
	int CountdownInputCount = 0;
	for(int i = 0; i < SwapList.m_Count && CountdownInputCount < (int)aCountdownInputs.size(); ++i)
	{
		const SSwapCountdownInfo &Info = SwapList.m_aInfos[i];
		aCountdownInputs[CountdownInputCount++] = QmHudMediaIslandSwapCountdownInput(Info.m_InstanceId, Info.m_StartTick, Info.m_Lifecycle, Info.m_Outgoing);
	}

	const int TickSpeed = Client()->GameTickSpeed();
	const int Team = GameClient()->SwitchStateTeam();
	const int SwitchNow = Client()->GameTick(g_Config.m_ClDummy);
	if(g_Config.m_QmSwitchCountdown &&
		QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode) &&
		TickSpeed > 0 && Team >= 0 && Team < NUM_DDRACE_TEAMS)
	{
		std::array<SHudMediaIslandCountdownInput, 255> aSwitchInputs{};
		int SwitchInputCount = 0;
		for(int Number = 1; Number < 256; ++Number)
		{
			const int EndTick = m_SwitchCountdownTracker.m_aaEndTick[Team][Number];
			const int TouchTick = m_SwitchCountdownTracker.m_aaTouchTick[Team][Number];
			if(EndTick <= SwitchNow || TouchTick <= 0)
				continue;
			SHudMediaIslandCountdownInput &Input = aSwitchInputs[SwitchInputCount++];
			const int InstanceId = QmHudMediaIslandSwitchInstanceId(Team, Number);
			Input = {EHudMediaIslandCountdownType::SWITCH, InstanceId, TouchTick, EndTick, EndTick - TouchTick};
			Input.m_Progress = QmHudMediaIslandCountdownProgress(Input, SwitchNow);
		}
		QmHudSortMediaIslandCountdowns(aSwitchInputs.data(), SwitchInputCount);
		const int VisibleSwitchCount = std::min(SwitchInputCount, SWITCH_COUNTDOWN_MAX_LINES);
		const int FirstVisibleSwitch = QmHudMediaIslandVisibleSuffixStart(SwitchInputCount, VisibleSwitchCount);
		for(int i = FirstVisibleSwitch; i < SwitchInputCount && CountdownInputCount < (int)aCountdownInputs.size(); ++i)
			aCountdownInputs[CountdownInputCount++] = aSwitchInputs[i];
	}

	if(m_MediaIslandMuteState.m_Confirmed && Now < m_MediaIslandMuteState.m_EndTick && CountdownInputCount < (int)aCountdownInputs.size())
	{
		SHudMediaIslandCountdownInput &Input = aCountdownInputs[CountdownInputCount++];
		Input = {EHudMediaIslandCountdownType::MUTE, 0, m_MediaIslandMuteState.m_TriggerTick, m_MediaIslandMuteState.m_EndTick, m_MediaIslandMuteState.m_DurationTicks};
		Input.m_Progress = QmHudMediaIslandCountdownProgress(Input, Now);
	}
	else if(m_MediaIslandMuteState.m_Confirmed && Now >= m_MediaIslandMuteState.m_EndTick)
	{
		m_MediaIslandMuteState.Reset();
	}
	QmHudSortMediaIslandCountdowns(aCountdownInputs.data(), CountdownInputCount);

	for(auto &Item : AnimState.m_aSatelliteItems)
		Item.m_Seen = false;
	for(int i = 0; i < CountdownInputCount; ++i)
	{
		const SHudMediaIslandCountdownInput &Input = aCountdownInputs[i];
		auto *pItem = static_cast<SHudMediaIslandAnimState::SSatelliteItem *>(nullptr);
		for(auto &Item : AnimState.m_aSatelliteItems)
		{
			if(Item.m_Used && Item.m_Type == Input.m_Type && Item.m_Id == Input.m_Id)
			{
				pItem = &Item;
				break;
			}
		}
		if(pItem == nullptr)
		{
			for(auto &Item : AnimState.m_aSatelliteItems)
			{
				if(!Item.m_Used)
				{
					pItem = &Item;
					break;
				}
			}
		}
		if(pItem == nullptr)
		{
			for(auto &Item : AnimState.m_aSatelliteItems)
			{
				if(!Item.m_Active)
				{
					Item.Reset();
					pItem = &Item;
					break;
				}
			}
		}
		if(pItem == nullptr)
			continue;

		if(!pItem->m_Used)
		{
			pItem->Reset();
			pItem->m_Used = true;
			pItem->m_Type = Input.m_Type;
			pItem->m_Id = Input.m_Id;
			pItem->m_TriggerTick = Input.m_TriggerTick;
		}
		pItem->m_Active = true;
		pItem->m_Seen = true;
		pItem->m_ExitStartTick = 0;
		pItem->m_Progress = Input.m_Progress;
		pItem->m_Completed = Input.m_Completed;
		pItem->m_SwapOutgoing = Input.m_SwapOutgoing;
	}

	for(auto &Item : AnimState.m_aSatelliteItems)
	{
		if(Item.m_Used && Item.m_Active && !Item.m_Seen)
		{
			Item.m_Active = false;
			Item.m_ExitStartTick = Now;
		}
	}
	const bool HadSatellitePresentation = AnimState.HasVisibleSatellite();
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
	char aFrozenSummaryBuf[64];
	const bool ShowFrozenSummary = BuildHudFrozenSummaryText(FrozenInfo, aFrozenSummaryBuf, sizeof(aFrozenSummaryBuf));

	const bool ShowLocalTime = ShouldRenderHudLocalTime(*GameClient());
	const SHudGameTimerInfo TimerInfo = g_Config.m_ClShowhudTimer ? BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width) : SHudGameTimerInfo{};
	SHudTopTimerCapsuleInfo TimerCapsule = BuildHudTopTimerCapsuleInfo(TimerInfo);
	char aRecordingBuf[512];
	const bool ShowRecordingStatus = TimerCapsule.m_Visible && BuildHudRecordingStatusText(*GameClient(), aRecordingBuf, sizeof(aRecordingBuf));
	const bool ScoreboardExpanded = GameClient()->m_Scoreboard.IsActive();
	const SHudRecordingPresentation RecordingPresentation = QmHudRecordingPresentation(ShowRecordingStatus, ScoreboardExpanded, ShowLocalTime, ShowFrozenSummary);
	const bool ShowInfoStack = RecordingPresentation.m_ShowInfoStack;
	const int SpectatorCount = m_MediaIslandFrameCache.m_SpectatorCount;
	const bool ShowSpectator = SpectatorCount > 0;
	const float SpectatorLiquidProgressBeforeUpdate = QmHudMediaIslandBlobProgress(AnimState.m_SpectatorLiquidSpring);
	const bool AnimateSpectatorEyeOpen = QmHudMediaIslandShouldAnimateSpectatorEyeOpen(ShowSpectator, AnimState.m_SpectatorHadWatchers, SpectatorLiquidProgressBeforeUpdate);
	if(ShowSpectator)
		AnimState.m_SpectatorDisplayCount = SpectatorCount;
	if(!ShowSpectator && AnimState.m_SpectatorHadWatchers)
	{
		AnimState.m_SpectatorExitLiquidStart = SpectatorLiquidProgressBeforeUpdate;
		AnimState.m_SpectatorExitIconStart = AnimState.m_SpectatorIconProgress;
	}
	QmHudAdvanceMediaIslandLiquidProgress(
		AnimState.m_SpectatorLiquidSpring,
		AnimState.m_SpectatorLiquidLastTick,
		Now,
		ShowSpectator,
		g_Config.m_QmUiMotionLevel > 0);
	if(!ShowSpectator && QmHudMediaIslandBlobProgress(AnimState.m_SpectatorLiquidSpring) <= 0.0f)
		AnimState.m_SpectatorLiquidLastTick = 0;
	if(!ShowSpectator)
	{
		AnimState.m_SpectatorIconProgress = QmHudMediaIslandSpectatorIconProgressDuringExit(AnimState.m_SpectatorExitIconStart, AnimState.m_SpectatorExitLiquidStart, QmHudMediaIslandBlobProgress(AnimState.m_SpectatorLiquidSpring));
		AnimState.m_SpectatorIconLastTick = 0;
	}
	else if(AnimateSpectatorEyeOpen)
	{
		AnimState.m_SpectatorIconLastTick = Now;
	}
	else if(!AnimState.m_SpectatorHadWatchers)
	{
		AnimState.m_SpectatorIconProgress = 1.0f;
		AnimState.m_SpectatorIconLastTick = 0;
	}
	if(ShowSpectator && AnimState.m_SpectatorIconProgress < 1.0f)
	{
		float SpectatorIconDeltaSeconds = 0.0f;
		if(AnimState.m_SpectatorIconLastTick > 0 && Now >= AnimState.m_SpectatorIconLastTick)
			SpectatorIconDeltaSeconds = std::min((Now - AnimState.m_SpectatorIconLastTick) / (float)time_freq(), 0.10f);
		AnimState.m_SpectatorIconLastTick = Now;
		AnimState.m_SpectatorIconProgress = QmHudAdvanceMediaIslandSpectatorIconProgress(AnimState.m_SpectatorIconProgress, SpectatorIconDeltaSeconds, g_Config.m_QmUiMotionLevel);
		if(AnimState.m_SpectatorIconProgress >= 1.0f)
			AnimState.m_SpectatorIconLastTick = 0;
	}
	AnimState.m_SpectatorHadWatchers = ShowSpectator;
	const bool HasSpectatorSatellitePresentation = ShowSpectator || QmHudMediaIslandBlobProgressOf(AnimState.m_SpectatorLiquidSpring.m_Progress) > 0.0f;
	const bool HasSatellitePresentation = HadSatellitePresentation || HasSpectatorSatellitePresentation;
	char aTeamBuf[32];
	const bool ShowTeam = BuildHudTeamText(*GameClient(), aTeamBuf, sizeof(aTeamBuf));
	const char *pLyricsIslandBuf = m_MediaIslandFrameCache.m_aLyrics;
	const ColorRGBA &LyricsIslandColor = m_MediaIslandFrameCache.m_LyricsColor;
	const bool ShowLyricsIslandLine = m_MediaIslandFrameCache.m_ShowLyrics || LyricsActive;
	const SHudMediaIslandSwapRows SwapRows = QmHudMediaIslandSwapRows(IncomingSwapCount, TimerCapsule.m_Visible, ShowLyricsIslandLine);
	const bool ShowTopRow = HasMediaState || ShowInfoStack || TimerCapsule.m_Visible || ShowRecordingStatus || ShowSpectator || ShowTeam;

	if(!ShowTopRow && !ShowLyricsIslandLine && !HasSatellitePresentation)
	{
		RelaxMediaIslandEntranceSprings(GameClient()->UiRuntimeV2()->AnimRuntime());
		m_MediaIslandAnimState.Reset();
		m_MediaIslandLastVisibleRectValid = false;
		return;
	}

	const int64_t AutoCollapseTicks = std::max<int64_t>(1, (int64_t)3000 * time_freq() / 1000);
	const bool WasLyricsActive = AnimState.m_LyricsActive;

	if(HasMediaState)
	{
		const bool HadMeaningfulTrackIdentity = AnimState.m_HasTrackIdentity && AnimState.m_CurrentTrack.HasMeaningfulIdentity();
		SHudMediaIslandTrackInput TrackInput;
		TrackInput.m_pTitle = MediaState.m_aTitle;
		TrackInput.m_pArtist = MediaState.m_aArtist;
		TrackInput.m_pAlbum = MediaState.m_aAlbum;
		TrackInput.m_Cover = MediaState.m_AlbumArtCircular;
		TrackInput.m_HasCover = MediaState.m_AlbumArtCircular.IsValid() && !MediaState.m_AlbumArtCircular.IsNullTexture();
		TrackInput.m_DurationMs = MediaState.m_DurationMs;
		const EHudMediaIslandTrackUpdate TrackUpdate = QmHudMediaIslandUpdateTrackSnapshots(
			AnimState.m_CurrentTrack,
			AnimState.m_OutgoingTrack,
			AnimState.m_HasTrackIdentity,
			AnimState.m_TrackTransitionActive,
			AnimState.m_TrackTransitionNeedsNodeReset,
			AnimState.m_TrackTransitionStartTick,
			Now,
			TrackInput);

		if(QmHudMediaIslandShouldRevealTrackDetails(
			   TrackUpdate,
			   HadMeaningfulTrackIdentity,
			   AnimState.m_CurrentTrack.HasMeaningfulIdentity()))
		{
			AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::EXPANDED;
			AnimState.m_ExpandUntilTick = Now + AutoCollapseTicks;
			AnimState.m_TrackDetailsUntilTick = Now + AutoCollapseTicks;
			AnimState.StartCapsuleMorph();
		}
	}
	else if(AnimState.m_HasTrackIdentity && !LyricsActive)
	{
		AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::MINIMIZED;
		AnimState.m_ExpandUntilTick = 0;
		AnimState.m_HasTrackIdentity = false;
		AnimState.m_CurrentTrack.Reset();
		AnimState.m_OutgoingTrack.Reset();
		AnimState.m_TrackTransitionActive = false;
		AnimState.m_TrackTransitionNeedsNodeReset = false;
		AnimState.m_TrackTransitionStartTick = 0;
		AnimState.m_OldTrackExitProgress = 1.0f;
		AnimState.m_NewTrackEnterProgress = 1.0f;
		AnimState.m_TrackDetailsUntilTick = 0;
	}

	if(LyricsActive || (HasMediaState && AlwaysShowTrackDetails))
	{
		if(AnimState.m_VisualState != SHudMediaIslandAnimState::EVisualState::EXPANDED)
		{
			AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::EXPANDED;
			AnimState.StartCapsuleMorph();
		}
	}
	else
	{
		if(WasLyricsActive && HasMediaState && AnimState.m_VisualState == SHudMediaIslandAnimState::EVisualState::EXPANDED)
			AnimState.m_ExpandUntilTick = Now + AutoCollapseTicks;
		if(AnimState.m_VisualState == SHudMediaIslandAnimState::EVisualState::EXPANDED && AnimState.m_ExpandUntilTick > 0 && Now >= AnimState.m_ExpandUntilTick)
		{
			AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::MINIMIZED;
			AnimState.m_ExpandUntilTick = 0;
			AnimState.m_TrackDetailsUntilTick = 0;
			AnimState.StartCapsuleMorph();
		}
	}
	AnimState.m_LyricsActive = LyricsActive;
	const bool TrackDetailsExpanded = HasMediaState && QmHudMediaIslandShouldShowTrackDetails(Now, AnimState.m_TrackDetailsUntilTick, AlwaysShowTrackDetails);
	const char *pDisplayTitle = "";
	if(HasMediaState)
	{
		if(AnimState.m_CurrentTrack.m_aTitle[0] != '\0')
			pDisplayTitle = AnimState.m_CurrentTrack.m_aTitle;
		else if(AnimState.m_CurrentTrack.m_aArtist[0] != '\0')
			pDisplayTitle = AnimState.m_CurrentTrack.m_aArtist;
		else if(AnimState.m_CurrentTrack.m_aAlbum[0] != '\0')
			pDisplayTitle = AnimState.m_CurrentTrack.m_aAlbum;
	}

	char aSpectatorBuf[16];
	str_format(aSpectatorBuf, sizeof(aSpectatorBuf), "%d", std::max(0, AnimState.m_SpectatorDisplayCount));

	char aTimeBuf[16];
	str_timestamp_format(aTimeBuf, sizeof(aTimeBuf), "%H:%M");
	if(aTimeBuf[0] == '\0')
		str_copy(aTimeBuf, "00:00", sizeof(aTimeBuf));

	const int Checkpoint = GetDisplayedCheckpoint(*GameClient());
	char aCheckpointBuf[16];
	aCheckpointBuf[0] = '\0';
	if(Checkpoint > 0)
		str_format(aCheckpointBuf, sizeof(aCheckpointBuf), "CP%d", Checkpoint);

	constexpr float BaseIslandHeight = QmHudMediaIslandScaled(16.0f);
	const float IslandY = 0.0f;
	constexpr float CoverSize = QmHudMediaIslandScaled(12.0f);
	constexpr float PaddingX = QmHudMediaIslandScaled(2.0f);
	constexpr float Gap = QmHudMediaIslandScaled(2.0f);
	constexpr float SpectatorGap = QmHudMediaIslandScaled(1.0f);
	constexpr float SpectatorSatellitePaddingX = QmHudMediaIslandScaled(2.0f);
	constexpr float SpectatorSatelliteIconSize = QmHudMediaIslandScaled(6.4f);
	constexpr float SpectatorSatelliteRestGap = QmHudMediaIslandScaled(3.0f);
	constexpr float TitleFontSize = QmHudMediaIslandScaled(5.8f);
	constexpr float MetaFontSize = QmHudMediaIslandScaled(5.3f);
	constexpr float ScreenPadding = 5.0f;
	constexpr float GapToTimer = QmHudMediaIslandScaled(3.0f);
	constexpr float TimerToStatusGap = QmHudMediaIslandScaled(3.0f);
	constexpr float StatusPaddingLeft = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusPaddingRight = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotSize = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotGap = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusFontSize = QmHudMediaIslandScaled(5.3f);
	constexpr float StackedStatusFontSize = QmHudMediaIslandScaled(4.4f);
	constexpr float InfoStackGap = QmHudMediaIslandScaled(0.8f);
	constexpr float WaveformSlotWidth = QmHudMediaIslandScaled(12.0f);
	constexpr float BottomFontSize = QmHudMediaIslandScaled(5.2f);
	constexpr float LyricsFontSize = BottomFontSize * 1.2f;
	constexpr float BottomRowPaddingX = QmHudMediaIslandScaled(7.0f);
	constexpr float BottomRowLineHeight = QmHudMediaIslandScaled(7.0f);
	constexpr float BottomRowPaddingY = QmHudMediaIslandScaled(2.5f);
	constexpr float BottomRowDividerInset = QmHudMediaIslandScaled(7.0f);
	const float MaxUnifiedWidth = std::max(0.0f, m_Width - ScreenPadding * 2.0f);
	const float MaxTitleWidth = std::clamp(m_Width * 0.18f * QmHudMediaIslandDesignScale, QmHudMediaIslandScaled(42.0f), QmHudMediaIslandScaled(88.0f));
	float IncomingSwapTextWidth = 0.0f;
	for(int i = 0; i < IncomingSwapCount; ++i)
		IncomingSwapTextWidth = std::max(IncomingSwapTextWidth, std::round(TextRender()->TextBoundingBox(BottomFontSize, aIncomingSwapInfos[i]->m_aRequestText).m_W));

	const bool ShowWaveform = HasMediaState;
	const float TeamIconWidth = MetaFontSize;
	const float PlaceholderWidth = MetaFontSize;
	const float SpectatorTextWidth = HasSpectatorSatellitePresentation ? TextRender()->TextWidth(MetaFontSize, aSpectatorBuf) : 0.0f;
	const float SpectatorSatelliteContentWidth = SpectatorSatelliteIconSize + SpectatorGap + SpectatorTextWidth;
	const float SpectatorSatelliteWidth = std::max(BaseIslandHeight, SpectatorSatelliteContentWidth + SpectatorSatellitePaddingX * 2.0f);
	const float TeamTextWidth = ShowTeam ? TextRender()->TextWidth(MetaFontSize, aTeamBuf) : 0.0f;
	const float TeamWidth = ShowTeam ? TeamIconWidth + SpectatorGap + TeamTextWidth : 0.0f;
	const float StatusTextWidth = ShowRecordingStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aRecordingBuf).m_W) : 0.0f;
	const float RawCollapsedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusPaddingRight;
	const float RawExpandedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusDotGap + StatusTextWidth + StatusPaddingRight;
	const bool StackHasTwoRows = ShowLocalTime && ShowFrozenSummary;
	const float InfoStackFontSize = StackHasTwoRows ? StackedStatusFontSize : StatusFontSize;
	const float LocalTimeTextWidth = ShowLocalTime ? std::round(TextRender()->TextBoundingBox(InfoStackFontSize, aTimeBuf).m_W) : 0.0f;
	const float FrozenSummaryTextWidth = ShowFrozenSummary ? std::round(TextRender()->TextBoundingBox(InfoStackFontSize, aFrozenSummaryBuf).m_W) : 0.0f;
	const float InfoStackWidth = ShowInfoStack ? StatusPaddingLeft + std::max(LocalTimeTextWidth, FrozenSummaryTextWidth) + StatusPaddingRight : 0.0f;
	float PlannedStatusWidth = 0.0f;
	if(ShowInfoStack)
		PlannedStatusWidth = InfoStackWidth;
	else if(ShowRecordingStatus)
		PlannedStatusWidth = ScoreboardExpanded ? RawExpandedStatusWidth : RawCollapsedStatusWidth;
	const bool ShowBottomRow = SwapRows.m_BottomLineCount > 0;
	const int BottomRowLineCount = SwapRows.m_BottomLineCount;
	const float DesiredBottomUnifiedWidth = QmHudMediaIslandDesiredBottomWidth(
		ShowLyricsIslandLine,
		ShowTopRow,
		IncomingSwapCount > 0,
		IncomingSwapTextWidth,
		MaxTitleWidth,
		MaxUnifiedWidth,
		BottomRowPaddingX);
	const bool ShowCover = HasMediaState;
	const int MetaItemCount = (ShowTeam ? 1 : 0) + (ShowWaveform ? 1 : 0);
	float BaseWidth = PaddingX;
	if(ShowCover)
		BaseWidth += CoverSize;
	if(ShowCover && MetaItemCount > 0)
		BaseWidth += Gap;
	if(ShowTeam)
		BaseWidth += TeamWidth;
	if(ShowTeam && ShowWaveform)
		BaseWidth += Gap;
	if(ShowWaveform)
		BaseWidth += WaveformSlotWidth;
	BaseWidth += PaddingX;
	if(!ShowCover && MetaItemCount == 0)
		BaseWidth = 0.0f;
	// 状态区（时钟/冰冻统计）在计时器右侧，不属于主胶囊内容，不能作为保留依据。
	if(QmHudMediaIslandShouldReserveMainCapsule(HasMediaState, ShowTeam, AnimState.HasVisibleCountdownSatellite(), TimerCapsule.m_Visible))
		BaseWidth = std::max(BaseWidth, BaseIslandHeight);
	if(SwapRows.m_InlineSwapCount > 0)
	{
		const float ReservedSideWidth = std::max(BaseWidth + (BaseWidth > 0.0f ? GapToTimer : 0.0f), PlannedStatusWidth + (PlannedStatusWidth > 0.0f ? TimerToStatusGap : 0.0f));
		const float MaxTimerWidth = std::max(TimerCapsule.m_BoxW, MaxUnifiedWidth - ReservedSideWidth * 2.0f);
		TimerCapsule.m_BoxW = std::min(MaxTimerWidth, std::max(TimerCapsule.m_BoxW, IncomingSwapTextWidth + BottomRowPaddingX * 2.0f));
	}
	const float MaxIslandWidth = TimerCapsule.m_Visible ?
					     std::max(BaseWidth, (MaxUnifiedWidth - TimerCapsule.m_BoxW) * 0.5f - GapToTimer) :
					     BaseWidth + (ShowCover ? (Gap + MaxTitleWidth) : 0.0f);
	const float MaxExpandedTitleWidth = std::max(0.0f, MaxIslandWidth - BaseWidth - Gap);
	char aLayoutTrackMeta[256];
	aLayoutTrackMeta[0] = '\0';
	if(AnimState.m_CurrentTrack.m_aArtist[0] != '\0' && AnimState.m_CurrentTrack.m_aAlbum[0] != '\0')
		str_format(aLayoutTrackMeta, sizeof(aLayoutTrackMeta), "%s - %s", AnimState.m_CurrentTrack.m_aArtist, AnimState.m_CurrentTrack.m_aAlbum);
	else if(AnimState.m_CurrentTrack.m_aArtist[0] != '\0')
		str_copy(aLayoutTrackMeta, AnimState.m_CurrentTrack.m_aArtist, sizeof(aLayoutTrackMeta));
	else if(AnimState.m_CurrentTrack.m_aAlbum[0] != '\0')
		str_copy(aLayoutTrackMeta, AnimState.m_CurrentTrack.m_aAlbum, sizeof(aLayoutTrackMeta));
	const float NaturalTitleWidth = std::round(std::max(TextRender()->TextBoundingBox(TitleFontSize, pDisplayTitle).m_W, aLayoutTrackMeta[0] != '\0' ? TextRender()->TextBoundingBox(MetaFontSize, aLayoutTrackMeta).m_W : 0.0f));
	const float TitleWidth = (TrackDetailsExpanded && ShowCover) ? std::clamp(NaturalTitleWidth, 0.0f, std::min(MaxTitleWidth, MaxExpandedTitleWidth)) : 0.0f;
	float TargetWidth = BaseWidth;
	if(TrackDetailsExpanded && TitleWidth > 0.0f)
		TargetWidth += Gap + TitleWidth;
	float MainToTimerGap = TargetWidth > 0.0f ? GapToTimer : 0.0f;
	float PlannedUnifiedWidth = TimerCapsule.m_Visible ?
					    (TargetWidth + MainToTimerGap + TimerCapsule.m_BoxW + (PlannedStatusWidth > 0.0f ? (TimerToStatusGap + PlannedStatusWidth) : 0.0f)) :
					    (TargetWidth + (TargetWidth > 0.0f && PlannedStatusWidth > 0.0f ? TimerToStatusGap : 0.0f) + PlannedStatusWidth);
	if(DesiredBottomUnifiedWidth > PlannedUnifiedWidth)
	{
		const float ExtraWidth = DesiredBottomUnifiedWidth - PlannedUnifiedWidth;
		TargetWidth += ExtraWidth;
		if(TimerCapsule.m_Visible)
			TargetWidth = std::min(TargetWidth, MaxIslandWidth);
		MainToTimerGap = TargetWidth > 0.0f ? GapToTimer : 0.0f;
		PlannedUnifiedWidth = TimerCapsule.m_Visible ?
					      (TargetWidth + MainToTimerGap + TimerCapsule.m_BoxW + (PlannedStatusWidth > 0.0f ? (TimerToStatusGap + PlannedStatusWidth) : 0.0f)) :
					      (TargetWidth + (TargetWidth > 0.0f && PlannedStatusWidth > 0.0f ? TimerToStatusGap : 0.0f) + PlannedStatusWidth);
	}
	const float MaxTargetX = std::max(ScreenPadding, m_Width - ScreenPadding - PlannedUnifiedWidth);
	const float TargetX = TimerCapsule.m_Visible ?
				      QmHudMediaIslandAnchorTimer(m_Width * 0.5f, TimerCapsule.m_BoxW, TargetWidth, MainToTimerGap).m_MainX :
				      std::clamp(m_Width * 0.5f - PlannedUnifiedWidth * 0.5f, ScreenPadding, MaxTargetX);
	const float TargetBottomHeight = ShowBottomRow ? (BottomRowPaddingY * 2.0f + BottomRowLineHeight * BottomRowLineCount) : 0.0f;
	const float TargetHeight = BaseIslandHeight + TargetBottomHeight;
	const float TitleAlphaTarget = TrackDetailsExpanded && TitleWidth > 0.0f ? 1.0f : 0.0f;
	const float TitleOffsetTarget = TrackDetailsExpanded ? 0.0f : QmHudMediaIslandScaled(4.0f);
	const float BottomAlphaTarget = ShowBottomRow ? 1.0f : 0.0f;
	const int MotionLevel = std::clamp(g_Config.m_QmUiMotionLevel, 0, 2);
	const bool FullTrackMotion = MotionLevel >= 2;
	const float TrackTextOffset = FullTrackMotion ? QmHudMediaIslandScaled(5.0f) : 0.0f;
	const float CoverEnterScale = FullTrackMotion ? 0.95f : 1.0f;
	const float CoverExitScale = FullTrackMotion ? 0.96f : 1.0f;
	SUiSpringConfig CapsuleSpring;
	CapsuleSpring.m_Stiffness = FullTrackMotion ? 430.0f : 520.0f;
	CapsuleSpring.m_Damping = FullTrackMotion ? 38.0f : 48.0f;
	CapsuleSpring.m_RestEpsilon = 0.025f;
	CapsuleSpring.m_RestVelocity = 0.16f;
	SUiSpringConfig SatelliteSpring = CapsuleSpring;
	SatelliteSpring.m_Stiffness = FullTrackMotion ? 500.0f : 620.0f;
	SatelliteSpring.m_Damping = FullTrackMotion ? 39.0f : 54.0f;
	SatelliteSpring.m_RestEpsilon = 0.012f;
	SatelliteSpring.m_RestVelocity = 0.10f;
	const auto BuildContentSpring = [FullTrackMotion](bool Entering) {
		SUiSpringConfig Spring;
		Spring.m_Stiffness = FullTrackMotion ? 360.0f : 520.0f;
		Spring.m_Damping = FullTrackMotion ? 34.0f : 52.0f;
		Spring.m_RestEpsilon = 0.008f;
		Spring.m_RestVelocity = 0.08f;
		if(!Entering)
		{
			constexpr float ExitTimeScale = 0.34f;
			Spring.m_Stiffness /= ExitTimeScale * ExitTimeScale;
			Spring.m_Damping /= ExitTimeScale;
			Spring.m_RestVelocity /= ExitTimeScale;
		}
		return Spring;
	};
	SUiSpringConfig ContentSpring = BuildContentSpring(true);
	SUiSpringConfig ContentExitSpring = BuildContentSpring(false);
	const SUiSpringConfig TitleSpring = TitleAlphaTarget > 0.0f ? ContentSpring : ContentExitSpring;
	const SUiSpringConfig BottomSpring = BottomAlphaTarget > 0.0f ? ContentSpring : ContentExitSpring;
	SUiSpringConfig TrackSpring;
	TrackSpring.m_Stiffness = FullTrackMotion ? 520.0f : 650.0f;
	TrackSpring.m_Damping = FullTrackMotion ? 38.0f : 58.0f;
	TrackSpring.m_RestEpsilon = 0.008f;
	TrackSpring.m_RestVelocity = 0.10f;
	SUiSpringConfig TrackExitSpring = TrackSpring;
	{
		constexpr float ExitTimeScale = 0.42f;
		TrackExitSpring.m_Stiffness /= ExitTimeScale * ExitTimeScale;
		TrackExitSpring.m_Damping /= ExitTimeScale;
		TrackExitSpring.m_RestVelocity /= ExitTimeScale;
	}
	float EffectiveTargetX = TargetX;
	float EffectiveTargetWidth = TargetWidth;
	float EffectiveTargetHeight = TargetHeight;

	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	const uint64_t CapsuleNode = HudMediaIslandNodeKey("capsule");
	const uint64_t TitleNode = HudMediaIslandNodeKey("title");
	const uint64_t BottomNode = HudMediaIslandNodeKey("bottom");
	const uint64_t CoverInNode = HudMediaIslandNodeKey("cover_in");
	const uint64_t CoverOutNode = HudMediaIslandNodeKey("cover_out");
	const uint64_t TrackTitleInNode = HudMediaIslandNodeKey("track_title_in");
	const uint64_t TrackTitleOutNode = HudMediaIslandNodeKey("track_title_out");
	const uint64_t TrackMetaInNode = HudMediaIslandNodeKey("track_meta_in");
	const uint64_t TrackMetaOutNode = HudMediaIslandNodeKey("track_meta_out");
	// 入场弹簧：掉落→展开两阶段（阶段语义保留，隐藏时回落、重现时继承速度）。
	const SHudMediaIslandEntranceSpringResult EntranceSprings = QmHudMediaIslandResolveEntranceSprings(
		AnimRuntime, HudMediaIslandNodeKey("entrance_drop"), HudMediaIslandNodeKey("entrance_expand"), true);
	// 胶囊挤压弹簧：展开/折叠时的 squeeze 形态（响应≈0.18s，ζ≈0.95）。
	SUiSpringConfig MorphSpring;
	MorphSpring.m_Stiffness = 1200.0f;
	MorphSpring.m_Damping = 66.0f;
	MorphSpring.m_RestEpsilon = 0.002f;
	MorphSpring.m_RestVelocity = 0.02f;
	const uint64_t MorphNode = HudMediaIslandNodeKey("capsule_morph");
	if(!AnimState.m_LayoutInitialized)
	{
		AnimState.m_TargetX = TargetX;
		AnimState.m_TargetWidth = TargetWidth;
		AnimState.m_TargetHeight = TargetHeight;
		AnimState.m_TargetTitleAlpha = TitleAlphaTarget;
		AnimState.m_TargetTitleOffset = TitleOffsetTarget;
		AnimState.m_TargetSpectatorAlpha = 0.0f;
		AnimState.m_TargetBottomAlpha = BottomAlphaTarget;
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, TargetX);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, TargetWidth);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, TargetHeight);
		SetUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::ALPHA, TitleAlphaTarget);
		SetUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::POS_X, TitleOffsetTarget);
		SetUiPresentationStateValue(AnimRuntime, BottomNode, EUiAnimProperty::ALPHA, BottomAlphaTarget);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::ALPHA, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::SCALE, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::SCALE, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::ALPHA, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::POS_X, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::POS_X, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::ALPHA, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::POS_X, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::POS_X, 0.0f);
		AnimState.m_LayoutInitialized = true;
	}

	if(AnimState.m_TrackTransitionNeedsNodeReset)
	{
		const float CurrentCoverAlpha = AnimRuntime.GetValue(CoverInNode, EUiAnimProperty::ALPHA, 1.0f);
		const float CurrentCoverScale = AnimRuntime.GetValue(CoverInNode, EUiAnimProperty::SCALE, 1.0f);
		const float CurrentTitleAlpha = AnimRuntime.GetValue(TrackTitleInNode, EUiAnimProperty::ALPHA, 1.0f);
		const float CurrentTitleOffset = AnimRuntime.GetValue(TrackTitleInNode, EUiAnimProperty::POS_X, 0.0f);
		const float CurrentMetaAlpha = AnimRuntime.GetValue(TrackMetaInNode, EUiAnimProperty::ALPHA, 1.0f);
		const float CurrentMetaOffset = AnimRuntime.GetValue(TrackMetaInNode, EUiAnimProperty::POS_X, 0.0f);

		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::ALPHA, CurrentCoverAlpha);
		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::SCALE, CurrentCoverScale);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::ALPHA, CurrentTitleAlpha);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::POS_X, CurrentTitleOffset);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::ALPHA, CurrentMetaAlpha);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::POS_X, CurrentMetaOffset);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::SCALE, CoverEnterScale);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::POS_X, TrackTextOffset);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::POS_X, TrackTextOffset);

		AnimState.m_TargetCoverOutAlpha = CurrentCoverAlpha;
		AnimState.m_TargetCoverOutScale = CurrentCoverScale;
		AnimState.m_TargetTrackTitleOutAlpha = CurrentTitleAlpha;
		AnimState.m_TargetTrackTitleOutOffset = CurrentTitleOffset;
		AnimState.m_TargetTrackMetaOutAlpha = CurrentMetaAlpha;
		AnimState.m_TargetTrackMetaOutOffset = CurrentMetaOffset;
		AnimState.m_TargetCoverInAlpha = 0.0f;
		AnimState.m_TargetCoverInScale = CoverEnterScale;
		AnimState.m_TargetTrackTitleInAlpha = 0.0f;
		AnimState.m_TargetTrackTitleInOffset = TrackTextOffset;
		AnimState.m_TargetTrackMetaInAlpha = 0.0f;
		AnimState.m_TargetTrackMetaInOffset = TrackTextOffset;
		AnimState.m_TrackTransitionNeedsNodeReset = false;
	}

	if(MotionLevel <= 0)
	{
		AnimState.m_CapsuleMorphActive = false;
		AnimState.m_CapsuleMorphNeedsCapture = false;
	}
	else if(AnimState.m_CapsuleMorphActive)
	{
		if(AnimState.m_CapsuleMorphNeedsCapture)
		{
			AnimState.m_CapsuleMorphFromX = AnimRuntime.GetValue(CapsuleNode, EUiAnimProperty::POS_X, AnimState.m_TargetX);
			AnimState.m_CapsuleMorphFromWidth = std::max(0.0f, AnimRuntime.GetValue(CapsuleNode, EUiAnimProperty::WIDTH, AnimState.m_TargetWidth));
			AnimState.m_CapsuleMorphFromHeight = std::max(0.0f, AnimRuntime.GetValue(CapsuleNode, EUiAnimProperty::HEIGHT, AnimState.m_TargetHeight));
			if(AnimState.m_CapsuleMorphFromWidth <= 0.01f)
				AnimState.m_CapsuleMorphFromWidth = TargetWidth;
			if(AnimState.m_CapsuleMorphFromHeight <= 0.01f)
				AnimState.m_CapsuleMorphFromHeight = TargetHeight;
			AnimState.m_CapsuleMorphNeedsCapture = false;
			// 新一次挤压从头开始：把 morph 弹簧进度瞬移到 0（目标仍为 1，弹簧重新起跳）。
			SetUiPresentationStateValue(AnimRuntime, MorphNode, EUiAnimProperty::ALPHA, 0.0f);
		}
	}

	// 挤压弹簧：morph 意图开启时目标 1，SqueezeAmount = 1 - MorphProgress 平滑衰减挤压量；
	// 弹簧落定后本次 morph 结束，目标回落 0（下次 morph 由 capture 重新从 0 起跳）。
	const float MorphTarget = AnimState.m_CapsuleMorphActive ? 1.0f : 0.0f;
	const float MorphProgress = ResolveUiPresentationStateValue(AnimRuntime, MorphNode, EUiAnimProperty::ALPHA, MorphTarget, MorphSpring, 3, 0.001f);
	if(AnimState.m_CapsuleMorphActive)
	{
		if(FullTrackMotion && MorphProgress < 1.0f)
		{
			const float FromCenterX = AnimState.m_CapsuleMorphFromX + AnimState.m_CapsuleMorphFromWidth * 0.5f;
			QmHudMediaIslandApplyCapsuleSqueeze(FromCenterX, AnimState.m_CapsuleMorphFromWidth, AnimState.m_CapsuleMorphFromHeight, 1.0f - MorphProgress, BaseIslandHeight, PaddingX, ScreenPadding, m_Width, EffectiveTargetX, EffectiveTargetWidth, EffectiveTargetHeight);
		}
		else if(MorphProgress >= 1.0f - 0.001f)
		{
			AnimState.m_CapsuleMorphActive = false;
		}
	}

	if(TimerCapsule.m_Visible)
		EffectiveTargetX = QmHudMediaIslandAnchorTimer(m_Width * 0.5f, TimerCapsule.m_BoxW, EffectiveTargetWidth, MainToTimerGap).m_MainX;
	AnimState.m_TargetX = EffectiveTargetX;
	AnimState.m_TargetWidth = EffectiveTargetWidth;
	AnimState.m_TargetHeight = EffectiveTargetHeight;
	AnimState.m_TargetTitleAlpha = TitleAlphaTarget;
	AnimState.m_TargetTitleOffset = TitleOffsetTarget;
	AnimState.m_TargetSpectatorAlpha = 0.0f;
	AnimState.m_TargetBottomAlpha = BottomAlphaTarget;
	AnimState.m_TargetCoverInAlpha = 1.0f;
	AnimState.m_TargetCoverOutAlpha = 0.0f;
	AnimState.m_TargetCoverInScale = 1.0f;
	AnimState.m_TargetCoverOutScale = CoverExitScale;
	AnimState.m_TargetTrackTitleInAlpha = 1.0f;
	AnimState.m_TargetTrackTitleOutAlpha = 0.0f;
	AnimState.m_TargetTrackTitleInOffset = 0.0f;
	AnimState.m_TargetTrackTitleOutOffset = -TrackTextOffset;
	AnimState.m_TargetTrackMetaInAlpha = 1.0f;
	AnimState.m_TargetTrackMetaOutAlpha = 0.0f;
	AnimState.m_TargetTrackMetaInOffset = 0.0f;
	AnimState.m_TargetTrackMetaOutOffset = -TrackTextOffset;

	const float IslandWidth = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, AnimState.m_TargetWidth, CapsuleSpring, 3, 0.01f);
	const float AnimatedIslandHeight = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, AnimState.m_TargetHeight, CapsuleSpring, 3, 0.01f);
	// 大时间独立锚定中线，左侧宽度弹簧和右侧扩展只改变各自的边界。
	const SHudMediaIslandTimerAnchor TimerAnchor = QmHudMediaIslandAnchorTimer(m_Width * 0.5f, TimerCapsule.m_BoxW, IslandWidth, MainToTimerGap);
	const float IslandX = TimerCapsule.m_Visible ? TimerAnchor.m_MainX : ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, AnimState.m_TargetX, CapsuleSpring, 3, 0.01f);
	if(TimerCapsule.m_Visible)
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, IslandX);
	const float TimerBoxX = TimerCapsule.m_Visible ? TimerAnchor.m_TimerX : TimerCapsule.m_BoxX;
	const float TimerBoxRight = TimerBoxX + TimerCapsule.m_BoxW;
	const float TitleAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTitleAlpha, TitleSpring, 2, 0.004f), 0.0f, 1.0f);
	const float TitleOffset = ResolveUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::POS_X, AnimState.m_TargetTitleOffset, TitleSpring, 2, 0.01f);
	const float BottomAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, BottomNode, EUiAnimProperty::ALPHA, AnimState.m_TargetBottomAlpha, BottomSpring, 2, 0.004f), 0.0f, 1.0f);
	const float CoverInAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::ALPHA, AnimState.m_TargetCoverInAlpha, TrackSpring, 2, 0.003f), 0.0f, 1.0f);
	const float CoverOutAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::ALPHA, AnimState.m_TargetCoverOutAlpha, TrackExitSpring, 2, 0.003f), 0.0f, 1.0f);
	const float CoverInScale = ResolveUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::SCALE, AnimState.m_TargetCoverInScale, TrackSpring, 2, 0.004f);
	const float CoverOutScale = ResolveUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::SCALE, AnimState.m_TargetCoverOutScale, TrackExitSpring, 2, 0.004f);
	const float TrackTitleInAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackTitleInAlpha, TrackSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackTitleOutAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackTitleOutAlpha, TrackExitSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackTitleInOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackTitleInOffset, TrackSpring, 2, 0.01f);
	const float TrackTitleOutOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackTitleOutOffset, TrackExitSpring, 2, 0.01f);
	const float TrackMetaInAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackMetaInAlpha, TrackSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackMetaOutAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackMetaOutAlpha, TrackExitSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackMetaInOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackMetaInOffset, TrackSpring, 2, 0.01f);
	const float TrackMetaOutOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackMetaOutOffset, TrackExitSpring, 2, 0.01f);
	AnimState.m_OldTrackExitProgress = 1.0f - CoverOutAlpha;
	AnimState.m_NewTrackEnterProgress = CoverInAlpha;
	if(AnimState.m_TrackTransitionActive && CoverOutAlpha <= 0.01f && TrackTitleOutAlpha <= 0.01f && CoverInAlpha >= 0.99f && TrackTitleInAlpha >= 0.99f)
	{
		AnimState.m_TrackTransitionActive = false;
		AnimState.m_OutgoingTrack.Reset();
		AnimState.m_OldTrackExitProgress = 1.0f;
		AnimState.m_NewTrackEnterProgress = 1.0f;
	}

	const float Radius = BaseIslandHeight * 0.5f;
	const float CoverX = IslandX + PaddingX;
	const float CoverY = IslandY + (BaseIslandHeight - CoverSize) * 0.5f;
	const vec2 CoverCenter(CoverX + CoverSize * 0.5f, CoverY + CoverSize * 0.5f);
	const float CoverRadius = CoverSize * 0.5f;
	const float RightMetaX = IslandX + IslandWidth - PaddingX;
	const float WaveformSlotX = ShowWaveform ? (RightMetaX - WaveformSlotWidth) : RightMetaX;
	const float MetaY = IslandY + (BaseIslandHeight - MetaFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);
	const float TeamAnchorX = ShowWaveform ? WaveformSlotX : RightMetaX;
	const float TeamX = ShowTeam ? (TeamAnchorX - (ShowWaveform ? Gap : 0.0f) - TeamWidth) : TeamAnchorX;
	const float TitleBaseX = CoverX + CoverSize + Gap;
	const float TitleX = TitleBaseX + TitleOffset;
	const float TitleRight = (ShowTeam ? TeamX : (ShowWaveform ? WaveformSlotX : (IslandX + IslandWidth - PaddingX))) - Gap;
	const float TitleAvailableWidth = std::max(0.0f, TitleRight - TitleX);
	const float TitleY = IslandY + (BaseIslandHeight - TitleFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);

	const float SatelliteRadius = Radius;
	const float SatelliteDiameter = SatelliteRadius * 2.0f;
	constexpr float SatelliteItemGap = QmHudMediaIslandScaled(2.0f);
	constexpr float SatelliteRestGap = QmHudMediaIslandScaled(3.0f);
	const float SatelliteRingRadius = SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_RADIUS_SCALE;
	const float SatelliteRingThickness = std::max(QmHudMediaIslandScaled(1.25f), SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_THICKNESS_SCALE);
	const float SatelliteIconSize = SatelliteRadius * 0.94f;
	const float SatelliteCenterY = IslandY + BaseIslandHeight * 0.5f;

	std::array<SHudMediaIslandAnimState::SSatelliteItem *, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aActiveSatelliteItems{};
	std::array<float, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aActiveSatelliteTargetWidths{};
	std::array<float, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aActiveSatelliteTargetCenters{};
	int ActiveSatelliteCount = 0;
	for(int i = 0; i < CountdownInputCount; ++i)
	{
		for(auto &Item : AnimState.m_aSatelliteItems)
		{
			if(Item.m_Used && Item.m_Active && Item.m_Type == aCountdownInputs[i].m_Type && Item.m_Id == aCountdownInputs[i].m_Id)
			{
				aActiveSatelliteItems[ActiveSatelliteCount++] = &Item;
				break;
			}
		}
	}

	float LogicalSatelliteWidth = 0.0f;
	for(int i = 0; i < ActiveSatelliteCount; ++i)
	{
		aActiveSatelliteTargetWidths[i] = SatelliteDiameter;
		LogicalSatelliteWidth += aActiveSatelliteTargetWidths[i];
	}
	if(ActiveSatelliteCount > 1)
		LogicalSatelliteWidth += SatelliteItemGap * (ActiveSatelliteCount - 1);
	const float TargetSatelliteX = ActiveSatelliteCount > 0 ? IslandX - SatelliteRestGap - LogicalSatelliteWidth : IslandX;
	float SatelliteCursorX = TargetSatelliteX;
	for(int i = 0; i < ActiveSatelliteCount; ++i)
	{
		aActiveSatelliteTargetCenters[i] = SatelliteCursorX + aActiveSatelliteTargetWidths[i] * 0.5f;
		SatelliteCursorX += aActiveSatelliteTargetWidths[i] + SatelliteItemGap;
	}
	AnimState.m_TargetSatelliteX = TargetSatelliteX;
	AnimState.m_TargetSatelliteWidth = LogicalSatelliteWidth;
	AnimState.m_TargetSatelliteAlpha = ActiveSatelliteCount > 0 ? 1.0f : 0.0f;

	struct SSatelliteRenderItem
	{
		EHudMediaIslandCountdownType m_Type = EHudMediaIslandCountdownType::SWAP;
		vec2 m_Center{};
		vec2 m_Radii{};
		float m_SmoothUnion = 0.0f;
		float m_ContentAlpha = 0.0f;
		float m_ContentScale = 1.0f;
		float m_Progress = 0.0f;
		bool m_Completed = false;
		bool m_SwapOutgoing = false;
	};
	static_assert(SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS == QmHudMediaIslandSdfMaxItems);
	std::array<SSatelliteRenderItem, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aSatelliteRenderItems{};
	int SatelliteRenderItemCount = 0;
	const bool CountdownUsesTimer = TimerCapsule.m_Visible && !HasMediaState && !ShowTeam;
	for(auto &Item : AnimState.m_aSatelliteItems)
	{
		if(!Item.m_Used)
			continue;

		int ActiveIndex = -1;
		for(int i = 0; i < ActiveSatelliteCount; ++i)
		{
			if(aActiveSatelliteItems[i] == &Item)
			{
				ActiveIndex = i;
				break;
			}
		}
		const bool Active = Item.m_Active && ActiveIndex >= 0;
		QmHudAdvanceMediaIslandLiquidProgress(Item.m_LiquidSpring, Item.m_LiquidLastTick, Now, Active, MotionLevel > 0);
		const SHudMediaIslandBlobPose BlobPose = QmHudMediaIslandBlobPose(Item.m_LiquidSpring);
		if(!Active && QmHudMediaIslandBlobProgress(Item.m_LiquidSpring) <= 0.0f)
		{
			Item.Reset();
			continue;
		}

		float FinalCenterX = Item.m_LiquidOriginCenterX;
		float FinalWidth = Item.m_LiquidOriginWidth;
		if(Active)
		{
			const float TargetCenterX = aActiveSatelliteTargetCenters[ActiveIndex];
			const float TargetWidth = aActiveSatelliteTargetWidths[ActiveIndex];
			const uint64_t ItemNode = HudMediaIslandSatelliteNodeKey(Item.m_Type, Item.m_Id);
			if(!Item.m_NodeInitialized)
			{
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::POS_X, TargetCenterX);
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::WIDTH, TargetWidth);
				Item.m_NodeInitialized = true;
			}
			if(MotionLevel <= 0)
			{
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::POS_X, TargetCenterX);
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::WIDTH, TargetWidth);
			}
			FinalCenterX = ResolveUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::POS_X, TargetCenterX, SatelliteSpring, 3, 0.004f);
			FinalWidth = ResolveUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::WIDTH, TargetWidth, SatelliteSpring, 3, 0.01f);
			Item.m_LiquidOriginCenterX = FinalCenterX;
			Item.m_LiquidOriginWidth = FinalWidth;
		}
		if(FinalCenterX == 0.0f)
			FinalCenterX = IslandX - SatelliteRestGap - SatelliteRadius;
		if(FinalWidth <= 0.0f)
			FinalWidth = SatelliteDiameter;

		const SHudMediaIslandSdfItem BlobItem = QmHudMediaIslandLeftBlobItem(
			IslandX, SatelliteCenterY, SatelliteRadius, FinalCenterX, FinalWidth, BlobPose, CountdownUsesTimer);

		SSatelliteRenderItem &RenderItem = aSatelliteRenderItems[SatelliteRenderItemCount++];
		RenderItem.m_Type = Item.m_Type;
		RenderItem.m_Center = BlobItem.m_Center;
		RenderItem.m_Radii = BlobItem.m_Radii;
		RenderItem.m_SmoothUnion = BlobItem.m_SmoothUnion;
		RenderItem.m_ContentAlpha = BlobItem.m_ContentAlpha;
		RenderItem.m_ContentScale = BlobItem.m_ContentScale;
		RenderItem.m_Progress = Item.m_Progress;
		RenderItem.m_Completed = Item.m_Completed;
		RenderItem.m_SwapOutgoing = Item.m_SwapOutgoing;
	}

	float SatelliteVisibleLeft = IslandX;
	for(int i = 0; i < SatelliteRenderItemCount; ++i)
	{
		const SSatelliteRenderItem &Item = aSatelliteRenderItems[i];
		SatelliteVisibleLeft = std::min(SatelliteVisibleLeft, Item.m_Center.x - Item.m_Radii.x - QmHudMediaIslandScaled(1.0f));
	}
	const unsigned int PrevFlags = TextRender()->GetRenderFlags();
	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);

	const float StatusAnchorRight = TimerCapsule.m_Visible ? TimerBoxRight : (IslandX + IslandWidth);
	const float StatusSectionGap = PlannedStatusWidth > 0.0f && (TimerCapsule.m_Visible || IslandWidth > 1.0f) ? TimerToStatusGap : 0.0f;
	const float StatusSectionX = StatusAnchorRight + StatusSectionGap;
	const float TargetStatusWidth = PlannedStatusWidth;
	const float TargetStatusAlpha = (ShowInfoStack || ShowRecordingStatus) ? 1.0f : 0.0f;
	const float TargetTextAlpha = RecordingPresentation.m_ShowRecordingText ? 1.0f : 0.0f;

	const uint64_t StatusBoxNode = HudRecordingStatusNodeKey("box");
	const uint64_t StatusTextNode = HudRecordingStatusNodeKey("text");
	if(!m_RecordingStatusAnimState.m_Initialized)
	{
		m_RecordingStatusAnimState.m_TargetWidth = TargetStatusWidth;
		m_RecordingStatusAnimState.m_TargetAlpha = TargetStatusAlpha;
		m_RecordingStatusAnimState.m_TargetTextAlpha = TargetTextAlpha;
		SetUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::WIDTH, TargetStatusWidth);
		SetUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::ALPHA, TargetStatusAlpha);
		SetUiPresentationStateValue(AnimRuntime, StatusTextNode, EUiAnimProperty::ALPHA, TargetTextAlpha);
		m_RecordingStatusAnimState.m_Initialized = true;
	}

	m_RecordingStatusAnimState.m_TargetWidth = TargetStatusWidth;
	m_RecordingStatusAnimState.m_TargetAlpha = TargetStatusAlpha;
	m_RecordingStatusAnimState.m_TargetTextAlpha = TargetTextAlpha;
	const SUiSpringConfig StatusSpring = TargetStatusAlpha > 0.0f ? ContentSpring : ContentExitSpring;
	const float StatusWidth = ResolveUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::WIDTH, m_RecordingStatusAnimState.m_TargetWidth, StatusSpring, 2, 0.01f);
	const float StatusAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::ALPHA, m_RecordingStatusAnimState.m_TargetAlpha, StatusSpring, 2, 0.004f), 0.0f, 1.0f);
	const float StatusTextAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, StatusTextNode, EUiAnimProperty::ALPHA, m_RecordingStatusAnimState.m_TargetTextAlpha, StatusSpring, 2, 0.004f), 0.0f, 1.0f);
	const bool RenderStatusSection = StatusWidth > 1.0f && StatusAlpha > 0.01f;
	const float UnifiedRight = StatusAnchorRight + (RenderStatusSection ? StatusSectionGap + StatusWidth : 0.0f);
	const float UnifiedWidth = std::max(IslandWidth, UnifiedRight - IslandX);
	const SHudMediaIslandBlobPose SpectatorBlobPose = QmHudMediaIslandBlobPose(AnimState.m_SpectatorLiquidSpring);
	const SHudMediaIslandSpectatorIconPose SpectatorIconPose = QmHudMediaIslandSpectatorIconPose(AnimState.m_SpectatorIconProgress);
	const SHudMediaIslandLiquidCapsule SpectatorLiquidCapsule = QmHudMediaIslandRightBlobCapsule(
		UnifiedRight,
		SatelliteCenterY,
		Radius,
		SpectatorSatelliteWidth,
		SpectatorSatelliteRestGap,
		SpectatorBlobPose);
	const float SpectatorVisibleRight = SpectatorLiquidCapsule.m_Rect.x + SpectatorLiquidCapsule.m_Rect.w + QmHudMediaIslandScaled(1.0f);
	const float EditorVisibleRight = std::max(UnifiedRight, SpectatorVisibleRight);
	// 屏幕外的副岛不参与整岛平移，避免边界约束把居中的大时间推走。
	const float VisibleLeft = TimerCapsule.m_Visible ? std::max(0.0f, SatelliteVisibleLeft) : SatelliteVisibleLeft;
	const float VisibleRight = TimerCapsule.m_Visible ? std::min(m_Width, EditorVisibleRight) : EditorVisibleRight;
	const CUIRect EditorVisibleRect = {VisibleLeft, IslandY, VisibleRight - VisibleLeft, AnimatedIslandHeight};
	const bool RenderLeftSection = ShowCover || ShowTeam || ShowWaveform;
	const bool ShowTimerSecondaryLine = SwapRows.m_InlineSwapCount > 0 || Checkpoint > 0;
	const SHudMediaIslandTimerRowLayout TimerRows = QmHudMediaIslandTimerRows(TimerCapsule.m_BoxY, TimerCapsule.m_BoxH, ShowTimerSecondaryLine);
	const float TimerRaceFontSize = std::min(TimerCapsule.m_FontSize, TimerRows.m_RaceH);
	const float TimerRaceTextWidth = TextRender()->TextWidth(TimerRaceFontSize, TimerCapsule.m_aText);
	const float TimerTextX = TimerBoxX + (TimerCapsule.m_BoxW - TimerRaceTextWidth) * 0.5f;
	const float TimerRaceTextY = TimerRows.m_RaceY + (TimerRows.m_RaceH - TimerRaceFontSize) * 0.5f;
	const float CheckpointFontSize = std::min(TimerCapsule.m_FontSize * 0.40f, TimerRows.m_CheckpointH);
	const float CheckpointTextY = TimerRows.m_CheckpointY + (TimerRows.m_CheckpointH - CheckpointFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);
	ColorRGBA IslandBackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHudIslandBgColor));
	IslandBackgroundColor.a = std::clamp(g_Config.m_QmHudIslandBgOpacity / 100.0f, 0.0f, 1.0f);
	m_MediaIslandLastVisibleRect = EditorVisibleRect;
	m_MediaIslandLastVisibleRectValid = true;

	float TransformedScreenX0, TransformedScreenY0, TransformedScreenX1, TransformedScreenY1;
	Graphics()->GetScreen(&TransformedScreenX0, &TransformedScreenY0, &TransformedScreenX1, &TransformedScreenY1);
	const CUIRect TargetMainIslandSdfRect = {IslandX, IslandY, UnifiedWidth, AnimatedIslandHeight};
	const SHudMediaIslandEntrancePose EntrancePose = QmHudMediaIslandEntrancePose(TargetMainIslandSdfRect, Radius, IslandBackgroundColor, EntranceSprings.m_ExpandProgress, EntranceSprings.m_DropProgress, TransformedScreenY0);
	const float EntranceContentAlpha = EntrancePose.m_ContentAlpha;
	const CUIRect MainIslandSdfRect = EntrancePose.m_Rect;
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.42f * EntranceContentAlpha);
	const float SpectatorCenterX = SpectatorLiquidCapsule.m_Rect.x + SpectatorLiquidCapsule.m_Rect.w * 0.5f;
	const float SpectatorCenterY = SpectatorLiquidCapsule.m_Rect.y + SpectatorLiquidCapsule.m_Rect.h * 0.5f;
	const SHudMediaIslandSdfCapsule SpectatorSdfCapsule = {
		{SpectatorCenterX - SpectatorLiquidCapsule.m_Rect.w * EntranceContentAlpha * 0.5f,
			SpectatorCenterY - SpectatorLiquidCapsule.m_Rect.h * EntranceContentAlpha * 0.5f,
			SpectatorLiquidCapsule.m_Rect.w * EntranceContentAlpha,
			SpectatorLiquidCapsule.m_Rect.h * EntranceContentAlpha},
		SpectatorLiquidCapsule.m_Radius * EntranceContentAlpha,
		SpectatorLiquidCapsule.m_SmoothUnion * EntranceContentAlpha};
	SHudMediaIslandSdfRenderState CurrentSdfState;
	for(int i = 0; i < SatelliteRenderItemCount; ++i)
	{
		const SSatelliteRenderItem &Item = aSatelliteRenderItems[i];
		SHudMediaIslandSdfItem &SdfItem = CurrentSdfState.m_Items[i];
		SdfItem.m_Center = Item.m_Center;
		SdfItem.m_Radii = Item.m_Radii * EntranceContentAlpha;
		SdfItem.m_SmoothUnion = Item.m_SmoothUnion * EntranceContentAlpha;
		SdfItem.m_ContentAlpha = Item.m_ContentAlpha * EntranceContentAlpha;
		SdfItem.m_ContentScale = Item.m_ContentScale * EntranceContentAlpha;
		SdfItem.m_CountdownProgress = Item.m_Progress;
		SdfItem.m_RingColor = MediaIslandCountdownColor(Item.m_Type);
	}

	const float ScreenPixelSize = std::max(
		(TransformedScreenX1 - TransformedScreenX0) / std::max(1, Graphics()->ScreenWidth()),
		(TransformedScreenY1 - TransformedScreenY0) / std::max(1, Graphics()->ScreenHeight()));
	CurrentSdfState.m_MainRect = MainIslandSdfRect;
	CurrentSdfState.m_MainRadius = EntrancePose.m_Radius;
	CurrentSdfState.m_MainCorners = IGraphics::CORNER_ALL;
	CurrentSdfState.m_MainDisabledCornerRadius = EntrancePose.m_DisabledCornerRadius;
	CurrentSdfState.m_ItemCount = SatelliteRenderItemCount;
	CurrentSdfState.m_HasRightCapsule = SpectatorSdfCapsule.m_Rect.w > 0.0f && SpectatorSdfCapsule.m_Rect.h > 0.0f;
	CurrentSdfState.m_RightCapsule = SpectatorSdfCapsule;
	CurrentSdfState.m_RingRadius = SatelliteRingRadius;
	CurrentSdfState.m_RingThickness = SatelliteRingThickness;
	CurrentSdfState.m_BackgroundColor = EntrancePose.m_BackgroundColor;
	CurrentSdfState.m_ScreenPixelSize = ScreenPixelSize;
	CurrentSdfState.m_OuterShadowSize = ScreenPixelSize * MEDIA_ISLAND_OUTER_SHADOW_PIXELS;
	CurrentSdfState.m_OuterShadowOpacity = MEDIA_ISLAND_OUTER_SHADOW_OPACITY * EntrancePose.m_BackgroundColor.a;
	CurrentSdfState.m_Rect = QmHudMediaIslandSdfOuterRect(CurrentSdfState);
	const IGraphics::CRenderTargetHandle Backdrop = MediaIslandBlurBackdrop();
	if(Backdrop.IsValid())
	{
		const CUIRect BackdropScreenRect = {
			TransformedScreenX0,
			TransformedScreenY0,
			TransformedScreenX1 - TransformedScreenX0,
			TransformedScreenY1 - TransformedScreenY0};
		CurrentSdfState.m_BackdropUv = QmHudMediaIslandBackdropUv(CurrentSdfState.m_Rect, BackdropScreenRect);
	}
	IGraphics::SMediaIslandSdfParams GpuSdfParams;
	if(QmHudMediaIslandBuildGpuSdfParams(CurrentSdfState, GpuSdfParams))
	{
		if(Graphics()->HasMediaIslandSdf())
			Graphics()->RenderMediaIslandSdf(GpuSdfParams, Backdrop);
		else
			DrawMediaIslandGeometryFallback(Graphics(), CurrentSdfState);
	}

	if(SpectatorLiquidCapsule.m_ContentAlpha * EntranceContentAlpha > 0.001f && SpectatorLiquidCapsule.m_Rect.w > 0.01f)
	{
		const float ContentScale = std::clamp(SpectatorBlobPose.m_RadiusScale, 0.0f, 1.0f) * EntranceContentAlpha;
		const float IconSize = SpectatorSatelliteIconSize * ContentScale;
		const float FontSize = MetaFontSize * ContentScale;
		const float TextWidth = SpectatorTextWidth * ContentScale;
		const float ContentGap = SpectatorGap * ContentScale;
		const float ContentWidth = IconSize + ContentGap + TextWidth;
		const float CenterX = SpectatorLiquidCapsule.m_Rect.x + SpectatorLiquidCapsule.m_Rect.w * 0.5f;
		const float ContentX = CenterX - ContentWidth * 0.5f;
		const auto IconRect = [&](float ScaleX, float ScaleY) {
			const float Width = IconSize * ScaleX;
			const float Height = IconSize * ScaleY;
			return CUIRect{ContentX + (IconSize - Width) * 0.5f, SatelliteCenterY - Height * 0.5f, Width, Height};
		};
		const float IconAlpha = 0.88f * SpectatorLiquidCapsule.m_ContentAlpha * EntranceContentAlpha;
		const auto RenderTextEye = [&](const char *pGlyph, const CUIRect &Rect, float Alpha) {
			if(Alpha <= 0.001f)
				return;
			const EFontPreset PreviousPreset = TextRender()->GetFontPreset();
			TextRender()->SetFontPreset(EFontPreset::QM_ICON_FONT);
			const float GlyphSize = Rect.h;
			const float GlyphWidth = TextRender()->TextWidth(GlyphSize, pGlyph);
			const ColorRGBA IconColor = ConfiguredQmUiIconColor(ColorRGBA(0.98f, 0.99f, 1.0f, IconAlpha * Alpha));
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(Rect.x + (Rect.w - GlyphWidth) * 0.5f, Rect.y));
			Cursor.m_FontSize = GlyphSize;
			Cursor.m_LineWidth = -1.0f;
			QmRenderImmediateFontIcon(*TextRender(), &Cursor, pGlyph, -1, IconColor, ConfiguredQmUiIconContrastColor(IconColor));
			TextRender()->SetFontPreset(PreviousPreset);
		};
		const CUIRect ClosedRect = IconRect(SpectatorIconPose.m_ClosedScale, SpectatorIconPose.m_ClosedScale);
		const CUIRect OpenRect = IconRect(SpectatorIconPose.m_OpenScaleX, SpectatorIconPose.m_OpenScaleY);
		RenderTextEye(FontIcons::FONT_ICON_EYE_SLASH, ClosedRect, SpectatorIconPose.m_ClosedAlpha);
		RenderTextEye(FontIcons::FONT_ICON_EYE, OpenRect, SpectatorIconPose.m_OpenAlpha);
		const float CountAlpha = QmHudMediaIslandSpectatorCountAlpha(ShowSpectator, SpectatorIconPose);
		if(CountAlpha > 0.001f)
		{
			TextRender()->TextColor(0.98f, 0.99f, 1.0f, 0.86f * SpectatorLiquidCapsule.m_ContentAlpha * EntranceContentAlpha * CountAlpha);
			TextRender()->Text(ContentX + IconSize + ContentGap + SpectatorIconPose.m_CountOffsetX * ContentScale, SatelliteCenterY - FontSize * 0.5f - QmHudMediaIslandScaled(0.5f), FontSize, aSpectatorBuf, -1.0f);
		}
	}

	for(int i = 0; i < SatelliteRenderItemCount; ++i)
	{
		const SSatelliteRenderItem &Item = aSatelliteRenderItems[i];
		if(Item.m_ContentAlpha * EntranceContentAlpha <= 0.001f || Item.m_ContentScale <= 0.01f)
			continue;
		const float IconSize = SatelliteIconSize * Item.m_ContentScale * EntranceContentAlpha;
		const CUIRect IconRect = {Item.m_Center.x - IconSize * 0.5f, Item.m_Center.y - IconSize * 0.5f, IconSize, IconSize};
		const ColorRGBA IconColor = Item.m_Completed ? ColorRGBA(0.20f, 1.0f, 0.42f, 0.96f * Item.m_ContentAlpha * EntranceContentAlpha) : ColorRGBA(0.98f, 0.99f, 1.0f, 0.94f * Item.m_ContentAlpha * EntranceContentAlpha);
		const EQmIcon Icon = MediaIslandCountdownIcon(Item.m_Type, Item.m_Completed, Item.m_SwapOutgoing);
		Ui()->DrawQmIcon(IconRect, Icon, MediaIslandCountdownFontIcon(Icon), IconColor);
	}

	const auto BuildTrackMetaText = [](const SHudMediaIslandTrackSnapshot &Track, char *pBuf, size_t BufSize) {
		pBuf[0] = '\0';
		if(Track.m_aArtist[0] != '\0' && Track.m_aAlbum[0] != '\0')
			str_format(pBuf, BufSize, "%s - %s", Track.m_aArtist, Track.m_aAlbum);
		else if(Track.m_aArtist[0] != '\0')
			str_copy(pBuf, Track.m_aArtist, BufSize);
		else if(Track.m_aAlbum[0] != '\0')
			str_copy(pBuf, Track.m_aAlbum, BufSize);
		return pBuf[0] != '\0';
	};
	const auto TrackDisplayTitle = [](const SHudMediaIslandTrackSnapshot &Track) {
		if(Track.m_aTitle[0] != '\0')
			return Track.m_aTitle;
		if(Track.m_aArtist[0] != '\0')
			return Track.m_aArtist;
		if(Track.m_aAlbum[0] != '\0')
			return Track.m_aAlbum;
		return "";
	};
	char aCurrentTrackMeta[256];
	char aOutgoingTrackMeta[256];
	const bool HasCurrentTrackMeta = BuildTrackMetaText(AnimState.m_CurrentTrack, aCurrentTrackMeta, sizeof(aCurrentTrackMeta));
	const bool HasOutgoingTrackMeta = BuildTrackMetaText(AnimState.m_OutgoingTrack, aOutgoingTrackMeta, sizeof(aOutgoingTrackMeta));
	const auto RenderTrackCover = [&](const SHudMediaIslandTrackSnapshot &Track, float Alpha, float Scale) {
		if(!ShowCover || Alpha <= 0.001f)
			return;

		const float CoverDrawRadius = CoverRadius * std::max(0.01f, Scale);
		if(Track.m_HasCover && Track.m_Cover.IsValid())
		{
			DrawTexturedQuad(Graphics(), Track.m_Cover, CoverCenter, CoverDrawRadius, Alpha);
			return;
		}

		DrawSmoothCircle(Graphics(), CoverCenter, CoverDrawRadius, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha));
		const float PlaceholderFontSize = MetaFontSize * Scale;
		Ui()->DrawQmIconAt(CoverCenter.x - PlaceholderWidth * Scale * 0.5f, CoverCenter.y - PlaceholderFontSize * 0.5f - QmHudMediaIslandScaled(0.5f), PlaceholderFontSize, EQmIcon::MUSIC, FontIcons::FONT_ICON_MUSIC, ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f * Alpha));
	};
	const auto RenderTrackText = [&](const SHudMediaIslandTrackSnapshot &Track, const char *pMeta, bool HasMeta, float TitleLayerAlpha, float MetaLayerAlpha, float TitleLayerOffset, float MetaLayerOffset) {
		if(!ShowCover || TitleAlpha <= 0.001f || TitleAvailableWidth <= QmHudMediaIslandScaled(2.0f))
			return;

		const char *pTitle = TrackDisplayTitle(Track);
		const float EffectiveTitleAlpha = TitleAlpha * TitleLayerAlpha;
		const float EffectiveMetaAlpha = TitleAlpha * MetaLayerAlpha;
		const bool RenderMeta = HasMeta && EffectiveMetaAlpha > 0.001f && TitleAvailableWidth > QmHudMediaIslandScaled(12.0f);
		const float TitleDrawY = RenderMeta ? (IslandY + QmHudMediaIslandScaled(2.0f)) : TitleY;
		if(pTitle[0] != '\0' && EffectiveTitleAlpha > 0.001f)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = TitleFontSize;
			Cursor.m_LineWidth = TitleAvailableWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(TitleX + TitleLayerOffset, TitleDrawY));
			TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.94f * EffectiveTitleAlpha);
			TextRender()->TextEx(&Cursor, pTitle);
		}
		if(RenderMeta)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = MetaFontSize;
			Cursor.m_LineWidth = TitleAvailableWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(TitleX + MetaLayerOffset, IslandY + QmHudMediaIslandScaled(8.3f)));
			TextRender()->TextColor(0.85f, 0.88f, 0.94f, 0.68f * EffectiveMetaAlpha);
			TextRender()->TextEx(&Cursor, pMeta);
		}
	};

	RenderTrackCover(AnimState.m_OutgoingTrack, CoverOutAlpha * EntranceContentAlpha, CoverOutScale);
	RenderTrackCover(AnimState.m_CurrentTrack, CoverInAlpha * EntranceContentAlpha, CoverInScale);

	if(ShowTeam)
	{
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.82f * EntranceContentAlpha);
		Ui()->DrawQmIconAt(TeamX, MetaY, MetaFontSize, EQmIcon::USERS, FontIcons::FONT_ICON_USERS, TextRender()->GetTextColor());
		TextRender()->Text(TeamX + TeamIconWidth + SpectatorGap, MetaY, MetaFontSize, aTeamBuf, -1.0f);
	}

	RenderTrackText(AnimState.m_OutgoingTrack, aOutgoingTrackMeta, HasOutgoingTrackMeta, TrackTitleOutAlpha * EntranceContentAlpha, TrackMetaOutAlpha * 0.88f * EntranceContentAlpha, TrackTitleOutOffset, TrackMetaOutOffset);
	RenderTrackText(AnimState.m_CurrentTrack, aCurrentTrackMeta, HasCurrentTrackMeta, TrackTitleInAlpha * EntranceContentAlpha, TrackMetaInAlpha * EntranceContentAlpha, TrackTitleInOffset, TrackMetaInOffset);

	if(ShowWaveform)
	{
		constexpr int WaveBarCount = 7;
		constexpr int WaveTargetBarCount = 6;
		constexpr float OriginalWaveBarWidth = QmHudMediaIslandScaled(1.15f);
		constexpr float OriginalWaveBarGap = QmHudMediaIslandScaled(0.70f);
		constexpr float WaveTargetWidth = WaveTargetBarCount * OriginalWaveBarWidth + (WaveTargetBarCount - 1) * OriginalWaveBarGap;
		constexpr float WaveNaturalWidth = WaveBarCount * OriginalWaveBarWidth + (WaveBarCount - 1) * OriginalWaveBarGap;
		constexpr float WaveCompression = WaveTargetWidth / WaveNaturalWidth;
		constexpr float WaveBarWidth = OriginalWaveBarWidth * WaveCompression;
		constexpr float WaveBarGap = OriginalWaveBarGap * WaveCompression;
		constexpr float WaveMaxHeight = QmHudMediaIslandScaled(7.2f);
		const float WaveWidth = WaveBarCount * WaveBarWidth + (WaveBarCount - 1) * WaveBarGap;
		const float WaveX = WaveformSlotX + (WaveformSlotWidth - WaveWidth) * 0.5f;
		const float WaveCenterY = IslandY + BaseIslandHeight * 0.5f;
		const float WaveTime = Now / static_cast<float>(time_freq());
		if(MediaState.m_Playing)
		{
			AnimState.m_WaveformWasPlaying = true;
			AnimState.m_WaveformSettling = false;
		}
		else if(AnimState.m_WaveformWasPlaying)
		{
			AnimState.m_WaveformWasPlaying = false;
			AnimState.m_WaveformSettling = true;
			AnimState.m_WaveformSettleStartTick = Now;
			AnimState.m_WaveformSettleSampleTime = WaveTime;
		}
		float WaveSettleElapsed = 0.0f;
		if(AnimState.m_WaveformSettling && Now >= AnimState.m_WaveformSettleStartTick)
			WaveSettleElapsed = (Now - AnimState.m_WaveformSettleStartTick) / static_cast<float>(time_freq());
		const float WaveSampleTime = AnimState.m_WaveformSettling ? AnimState.m_WaveformSettleSampleTime : WaveTime;
		const bool WaveMotionEnabled = g_Config.m_QmUiMotionLevel > 0;
		for(int Bar = 0; Bar < WaveBarCount; ++Bar)
		{
			float SettleProgress = 1.0f;
			if(WaveMotionEnabled && MediaState.m_Playing)
				SettleProgress = 0.0f;
			else if(WaveMotionEnabled && AnimState.m_WaveformSettling)
				SettleProgress = QmHudMediaIslandWaveBarSettleProgress(Bar, WaveBarCount, WaveSettleElapsed);
			const float Height = WaveMaxHeight * QmHudMediaIslandWaveBarHeight(Bar, WaveSampleTime, 1.0f - SettleProgress);
			Graphics()->DrawRect(
				WaveX + Bar * (WaveBarWidth + WaveBarGap),
				WaveCenterY - Height * 0.5f,
				WaveBarWidth,
				Height,
				ColorRGBA(0.92f, 0.96f, 1.0f, 0.88f * EntranceContentAlpha),
				IGraphics::CORNER_ALL,
				WaveBarWidth * 0.5f);
		}
	}

	if(TimerCapsule.m_Visible)
	{
		if(RenderLeftSection)
		{
			const float LeftDividerX = TimerBoxX - GapToTimer * 0.5f;
			Graphics()->DrawRect(LeftDividerX, IslandY + QmHudMediaIslandScaled(4.0f), QmHudMediaIslandScaled(0.75f), BaseIslandHeight - QmHudMediaIslandScaled(8.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * EntranceContentAlpha), IGraphics::CORNER_ALL, QmHudMediaIslandScaled(0.375f));
		}

		if(TimerCapsule.m_IsCritical)
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, TimerCapsule.m_Alpha * EntranceContentAlpha);
		else
			TextRender()->TextColor(0.98f, 0.99f, 1.0f, 0.98f * EntranceContentAlpha);
		TextRender()->Text(TimerTextX, TimerRaceTextY, TimerRaceFontSize, TimerCapsule.m_aText, -1.0f);
		if(SwapRows.m_InlineSwapCount > 0)
		{
			const char *pSwapText = aIncomingSwapInfos[0]->m_aRequestText;
			const float SwapTextWidth = TextRender()->TextWidth(CheckpointFontSize, pSwapText);
			const float SwapTextMaxWidth = std::max(0.0f, TimerCapsule.m_BoxW - BottomRowPaddingX * 2.0f);
			TextRender()->TextColor(0.86f, 0.94f, 1.0f, 0.92f * EntranceContentAlpha);
			if(SwapTextWidth <= SwapTextMaxWidth)
			{
				const float SwapTextX = TimerBoxX + (TimerCapsule.m_BoxW - SwapTextWidth) * 0.5f;
				TextRender()->Text(SwapTextX, CheckpointTextY, CheckpointFontSize, pSwapText, -1.0f);
			}
			else
			{
				CTextCursor Cursor;
				Cursor.m_FontSize = CheckpointFontSize;
				Cursor.m_LineWidth = SwapTextMaxWidth;
				Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
				Cursor.SetPosition(vec2(TimerBoxX + BottomRowPaddingX, CheckpointTextY));
				TextRender()->TextEx(&Cursor, pSwapText);
			}
		}
		else if(Checkpoint > 0)
		{
			const float CheckpointWidth = TextRender()->TextWidth(CheckpointFontSize, aCheckpointBuf);
			const float CheckpointTextX = TimerBoxX + std::max(0.0f, (TimerCapsule.m_BoxW - CheckpointWidth) * 0.5f);
			TextRender()->TextColor(0.86f, 0.89f, 0.95f, 0.82f * EntranceContentAlpha);
			TextRender()->Text(CheckpointTextX, CheckpointTextY, CheckpointFontSize, aCheckpointBuf, -1.0f);
		}
	}

	if(RenderStatusSection)
	{
		if(StatusSectionGap > 0.0f)
		{
			const float DividerX = StatusAnchorRight + StatusSectionGap * 0.5f;
			Graphics()->DrawRect(DividerX, IslandY + QmHudMediaIslandScaled(4.0f), QmHudMediaIslandScaled(0.75f), BaseIslandHeight - QmHudMediaIslandScaled(8.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * StatusAlpha * EntranceContentAlpha), IGraphics::CORNER_ALL, QmHudMediaIslandScaled(0.375f));
		}

		if(ShowInfoStack)
		{
			const auto RenderInfoLine = [&](const char *pText, float CenterY, const ColorRGBA &Color) {
				if(pText == nullptr || pText[0] == '\0')
					return;
				const float TextWidth = std::round(TextRender()->TextBoundingBox(InfoStackFontSize, pText).m_W);
				const float TextX = StatusSectionX + std::max(0.0f, (StatusWidth - TextWidth) * 0.5f);
				const float TextY = CenterY - InfoStackFontSize * 0.5f - QmHudMediaIslandScaled(0.4f);
				TextRender()->TextColor(Color);
				TextRender()->Text(TextX, TextY, InfoStackFontSize, pText, -1.0f);
			};

			if(StackHasTwoRows)
			{
				const SHudMediaIslandInfoStackLayout InfoStackLayout = QmHudMediaIslandMirroredInfoStack(IslandY, BaseIslandHeight, InfoStackFontSize, InfoStackGap);
				RenderInfoLine(aTimeBuf, InfoStackLayout.m_TopCenterY, ColorRGBA(0.98f, 0.99f, 1.0f, 0.94f * StatusAlpha * EntranceContentAlpha));
				RenderInfoLine(aFrozenSummaryBuf, InfoStackLayout.m_BottomCenterY, ColorRGBA(0.86f, 0.90f, 0.97f, 0.86f * StatusAlpha * EntranceContentAlpha));
			}
			else if(ShowLocalTime)
				RenderInfoLine(aTimeBuf, IslandY + BaseIslandHeight * 0.5f, ColorRGBA(0.98f, 0.99f, 1.0f, 0.94f * StatusAlpha * EntranceContentAlpha));
			else
				RenderInfoLine(aFrozenSummaryBuf, IslandY + BaseIslandHeight * 0.5f, ColorRGBA(0.97f, 0.98f, 1.0f, 0.92f * StatusAlpha * EntranceContentAlpha));
		}
		else
		{
			const vec2 DotCenter(StatusSectionX + StatusPaddingLeft + StatusDotSize * 0.5f, IslandY + BaseIslandHeight * 0.5f);
			DrawHudRecordingStatusDot(Graphics(), DotCenter, StatusDotSize, QmHudRecordingDotAlpha(time_get() / (double)time_freq()) * StatusAlpha * EntranceContentAlpha, ScreenPixelSize);

			if(RecordingPresentation.RecordingTextAlpha(StatusTextAlpha) > 0.001f && StatusWidth > RawCollapsedStatusWidth + QmHudMediaIslandScaled(2.0f))
			{
				const float StatusTextX = StatusSectionX + StatusPaddingLeft + StatusDotSize + StatusDotGap;
				const float StatusTextY = IslandY + (BaseIslandHeight - StatusFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);
				TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.90f * StatusTextAlpha * EntranceContentAlpha);
				TextRender()->Text(StatusTextX, StatusTextY, StatusFontSize, aRecordingBuf, -1.0f);
			}
		}
	}

	const float VisibleBottomAlpha = BottomAlpha * EntranceContentAlpha;
	if(VisibleBottomAlpha > 0.01f && AnimatedIslandHeight > BaseIslandHeight + QmHudMediaIslandScaled(0.5f))
	{
		const float BottomRowY = IslandY + BaseIslandHeight;
		const float DividerInset = std::min(BottomRowDividerInset, UnifiedWidth * 0.25f);
		const float DividerWidth = std::max(0.0f, UnifiedWidth - DividerInset * 2.0f);
		if(DividerWidth > 0.0f)
		{
			Graphics()->DrawRect(IslandX + DividerInset, BottomRowY, DividerWidth, QmHudMediaIslandScaled(0.75f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * VisibleBottomAlpha), IGraphics::CORNER_ALL, QmHudMediaIslandScaled(0.375f));
		}

		float BottomTextY = BottomRowY + BottomRowPaddingY;
		const float ContentX = IslandX + BottomRowPaddingX;
		const float ContentWidth = std::max(0.0f, UnifiedWidth - BottomRowPaddingX * 2.0f);
		if(!AnimState.m_LyricsMarqueeInitialized || QmHudMediaIslandShouldResetMarquee(AnimState.m_aLyricsMarquee, pLyricsIslandBuf))
		{
			str_copy(AnimState.m_aLyricsMarquee, pLyricsIslandBuf, sizeof(AnimState.m_aLyricsMarquee));
			AnimState.m_LyricsMarqueeStartTick = Now;
			AnimState.m_LyricsMarqueeInitialized = true;
		}
		const auto EnableMappedClip = [&](const CUIRect &Rect) {
			float ScreenX0 = 0.0f;
			float ScreenY0 = 0.0f;
			float ScreenX1 = 0.0f;
			float ScreenY1 = 0.0f;
			Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
			const float MappedWidth = std::max(0.001f, ScreenX1 - ScreenX0);
			const float MappedHeight = std::max(0.001f, ScreenY1 - ScreenY0);
			const float PixelScaleX = Graphics()->ScreenWidth() / MappedWidth;
			const float PixelScaleY = Graphics()->ScreenHeight() / MappedHeight;
			const int ClipX0 = std::clamp((int)std::floor((Rect.x - ScreenX0) * PixelScaleX), 0, Graphics()->ScreenWidth());
			const int ClipY0 = std::clamp((int)std::floor((Rect.y - ScreenY0) * PixelScaleY), 0, Graphics()->ScreenHeight());
			const int ClipX1 = std::clamp((int)std::ceil((Rect.x + Rect.w - ScreenX0) * PixelScaleX), 0, Graphics()->ScreenWidth());
			const int ClipY1 = std::clamp((int)std::ceil((Rect.y + Rect.h - ScreenY0) * PixelScaleY), 0, Graphics()->ScreenHeight());
			if(ClipX1 > ClipX0 && ClipY1 > ClipY0)
				Graphics()->ClipEnable(ClipX0, ClipY0, ClipX1 - ClipX0, ClipY1 - ClipY0);
			return ClipX1 > ClipX0 && ClipY1 > ClipY0;
		};
		const auto RenderBottomTextBlock = [&](float X, float Y, float MaxWidth, const char *pText, const ColorRGBA &Color, bool AlignRight) {
			if(pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
				return;

			const float TextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
			TextRender()->TextColor(Color);
			if(TextWidth <= MaxWidth + 0.01f)
			{
				const float DrawX = AlignRight ? (X + MaxWidth - TextWidth) : X;
				TextRender()->Text(DrawX, Y, BottomFontSize, pText, -1.0f);
				return;
			}

			CTextCursor Cursor;
			Cursor.m_FontSize = BottomFontSize;
			Cursor.m_LineWidth = MaxWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(X, Y));
			TextRender()->TextEx(&Cursor, pText);
		};

		const auto RenderBottomTextCentered = [&](float Y, const char *pText, const ColorRGBA &Color) {
			if(pText == nullptr || pText[0] == '\0' || ContentWidth <= 0.0f)
				return;

			const float TextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
			if(TextWidth <= ContentWidth + 0.01f)
			{
				TextRender()->TextColor(Color);
				TextRender()->Text(IslandX + (UnifiedWidth - TextWidth) * 0.5f, Y, BottomFontSize, pText, -1.0f);
				return;
			}

			RenderBottomTextBlock(ContentX, Y, ContentWidth, pText, Color, false);
		};

		for(int i = SwapRows.m_InlineSwapCount; i < IncomingSwapCount; ++i)
		{
			const int LineIndex = i - SwapRows.m_InlineSwapCount;
			RenderBottomTextCentered(
				BottomTextY + BottomRowLineHeight * LineIndex,
				aIncomingSwapInfos[i]->m_aRequestText,
				ColorRGBA(0.86f, 0.94f, 1.0f, 0.92f * VisibleBottomAlpha));
		}

		if(ShowLyricsIslandLine)
		{
			const float LyricsRowY = BottomTextY + BottomRowLineHeight * SwapRows.m_LyricsLineIndex;
			ColorRGBA LyricsTextColor = LyricsIslandColor;
			LyricsTextColor.a *= VisibleBottomAlpha;
			if(pLyricsIslandBuf[0] != '\0' && ContentWidth > 0.0f)
			{
				float LyricsVisualTop = 0.0f;
				float LyricsVisualBottom = 0.0f;
				STextSizeProperties LyricsTextSizeProps{};
				LyricsTextSizeProps.m_pVisualTop = &LyricsVisualTop;
				LyricsTextSizeProps.m_pVisualBottom = &LyricsVisualBottom;
				const float LyricsTextWidth = std::round(TextRender()->TextWidth(LyricsFontSize, pLyricsIslandBuf, -1, -1.0f, 0, LyricsTextSizeProps));
				const float ElapsedSeconds = AnimState.m_LyricsMarqueeStartTick > 0 && Now >= AnimState.m_LyricsMarqueeStartTick ?
								     (Now - AnimState.m_LyricsMarqueeStartTick) / (float)time_freq() :
								     0.0f;
				const float LyricsOffset = QmHudMediaIslandMarqueeOffset(LyricsTextWidth, ContentWidth, ElapsedSeconds, QmHudMediaIslandScaled(32.0f));
				const CUIRect LyricsRect = {ContentX, LyricsRowY, ContentWidth, BottomRowLineHeight};
				const float DrawX = LyricsTextWidth <= ContentWidth ? LyricsRect.x + (LyricsRect.w - LyricsTextWidth) * 0.5f : LyricsRect.x - LyricsOffset;
				const float DrawY = LyricsRect.y + LyricsRect.h * 0.5f - (LyricsVisualTop + LyricsVisualBottom) * 0.5f;
				TextRender()->TextColor(LyricsTextColor);
				if(EnableMappedClip(LyricsRect))
				{
					TextRender()->Text(DrawX, DrawY, LyricsFontSize, pLyricsIslandBuf, -1.0f);
					Graphics()->ClipDisable();
				}
			}
		}
	}

	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevOutlineColor);
	TextRender()->SetRenderFlags(PrevFlags);
}
