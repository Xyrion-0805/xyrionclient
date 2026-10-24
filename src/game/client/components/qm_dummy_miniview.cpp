// QmClient 分身小窗：原版布局和绘制，适配当前地图渲染接口。
#include "hud.h"
#include "qmclient/dummy_mini_view_layout.h"
#include "qmclient/dummy_miniview_render.h"
#include <base/str.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
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
}
namespace
{
	struct SHudDummyMiniViewState
	{
		bool m_HasSignal = false;
		char m_aPlaceholderTitle[48] = {};
		char m_aPlaceholderSubtitle[256] = {};
		ColorRGBA m_TargetAccent = ColorRGBA(0.35f, 0.78f, 1.0f, 1.0f);
	};

	bool IsDummyMiniViewPredictedLocalTarget(const CGameClient &GameClient, int MiniViewClientId)
	{
		return GameClient.PredictDummy() &&
		       MiniViewClientId >= 0 &&
		       MiniViewClientId == GameClient.m_aLocalIds[!g_Config.m_ClDummy];
	}

	bool TryGetDummyMiniViewTargetPos(const CGameClient &GameClient, const IClient &Client, int MiniViewClientId, vec2 &OutPos, bool *pFromSnapshot = nullptr)
	{
		if(pFromSnapshot != nullptr)
			*pFromSnapshot = false;

		if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
			return false;

		const CGameClient::CClientData &MiniClient = GameClient.m_aClients[MiniViewClientId];
		if(!MiniClient.m_Active)
			return false;

		if(GameClient.m_Snap.m_aCharacters[MiniViewClientId].m_Active)
		{
			OutPos = MiniClient.m_RenderPos;
			if(MiniClient.m_RenderCur.m_Tick < 0 && MiniClient.m_RenderPrev.m_Tick < 0)
				OutPos = vec2(MiniClient.m_Snapped.m_X, MiniClient.m_Snapped.m_Y);
			if(pFromSnapshot != nullptr)
				*pFromSnapshot = true;
			return true;
		}

		if(MiniViewClientId == GameClient.m_Snap.m_LocalClientId)
		{
			OutPos = GameClient.m_LocalCharacterPos;
			return true;
		}

		if(IsDummyMiniViewPredictedLocalTarget(GameClient, MiniViewClientId))
		{
			OutPos = mix(MiniClient.m_PrevPredicted.m_Pos, MiniClient.m_Predicted.m_Pos, Client.PredIntraGameTick(g_Config.m_ClDummy));
			return true;
		}

		return false;
	}

	void RenderHudEllipsizedText(ITextRender *pTextRender, float X, float Y, float FontSize, float MaxWidth, const char *pText)
	{
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
			return;

		const float TextWidth = std::round(pTextRender->TextBoundingBox(FontSize, pText).m_W);
		if(TextWidth <= MaxWidth + 0.01f)
		{
			pTextRender->Text(X, Y, FontSize, pText, -1.0f);
			return;
		}

		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = MaxWidth;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.SetPosition(vec2(X, Y));
		pTextRender->TextEx(&Cursor, pText);
	}

	SHudDummyMiniViewState BuildHudDummyMiniViewState(const CGameClient &GameClient, const IClient &Client, bool Preview, int DummyClientId, int MiniViewClientId)
	{
		SHudDummyMiniViewState State;
		const bool TargetIsDummy = MiniViewClientId >= 0 ? (MiniViewClientId == DummyClientId) : !g_Config.m_ClDummy;
		State.m_TargetAccent = TargetIsDummy ? ColorRGBA(0.35f, 0.78f, 1.0f, 1.0f) : ColorRGBA(1.0f, 0.74f, 0.34f, 1.0f);
		str_copy(State.m_aPlaceholderTitle, Localize("Dummy mini view"));

		if(MiniViewClientId >= 0 && MiniViewClientId < MAX_CLIENTS)
		{
			vec2 TargetPos;
			State.m_HasSignal = TryGetDummyMiniViewTargetPos(GameClient, Client, MiniViewClientId, TargetPos);
		}

		if(Preview)
		{
			str_copy(State.m_aPlaceholderSubtitle, Localize("HUD editor preview"));
		}
		else if(!Client.DummyConnected())
		{
			str_copy(State.m_aPlaceholderSubtitle, Localize("Connect dummy to enable"));
		}
		else
		{
			if(!State.m_HasSignal)
				str_copy(State.m_aPlaceholderSubtitle, Localize("Waiting for snapshot"));
		}

		return State;
	}

	bool IsDummyMiniViewTargetOutsideCurrentView(const CGameClient &GameClient, IGraphics *pGraphics, int MiniViewClientId)
	{
		if(!g_Config.m_QmDummyMiniViewAuto || pGraphics == nullptr)
			return true;
		if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
			return true;

		vec2 TargetPos;
		if(!TryGetDummyMiniViewTargetPos(GameClient, *GameClient.Client(), MiniViewClientId, TargetPos))
			return true;

		float ViewWidth = 0.0f;
		float ViewHeight = 0.0f;
		pGraphics->CalcScreenParams(pGraphics->GameScreenAspect(), GameClient.m_Camera.m_Zoom, &ViewWidth, &ViewHeight);

		const vec2 Center = GameClient.m_Camera.m_Center;
		constexpr float TileMargin = 32.0f;
		const float Left = Center.x - ViewWidth * 0.5f - TileMargin;
		const float Right = Center.x + ViewWidth * 0.5f + TileMargin;
		const float Top = Center.y - ViewHeight * 0.5f - TileMargin;
		const float Bottom = Center.y + ViewHeight * 0.5f + TileMargin;

		return TargetPos.x < Left || TargetPos.x > Right || TargetPos.y < Top || TargetPos.y > Bottom;
	}
}


void CHud::DestroyDummyMiniViewRenderTarget()
{
	if(m_DummyMiniViewGraphicsVersion == Graphics()->GraphicsResourcesResetVersion())
		Graphics()->DestroyRenderTarget(&m_DummyMiniViewRenderTarget);
	else
		m_DummyMiniViewRenderTarget.Invalidate();
	m_DummyMiniViewRenderTargetWidth = 0;
	m_DummyMiniViewRenderTargetHeight = 0;
}
bool CHud::GetDummyMiniMapRect(float &X, float &Y, float &W, float &H) const
{
	if(!g_Config.m_QmDummyMiniView)
		return false;
	if(!Client()->DummyConnected())
		return false;

	const int DummyClientId = GameClient()->m_aLocalIds[1];
	const int MainClientId = GameClient()->m_aLocalIds[0];
	if(DummyClientId < 0 || DummyClientId >= MAX_CLIENTS)
		return false;
	const int MiniViewClientId = g_Config.m_ClDummy ? MainClientId : DummyClientId;
	if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
		return false;
	const CGameClient::CClientData &MiniClient = GameClient()->m_aClients[MiniViewClientId];
	if(!MiniClient.m_Active)
		return false;
	vec2 TargetPos;
	if(!TryGetDummyMiniViewTargetPos(*GameClient(), *Client(), MiniViewClientId, TargetPos))
		return false;
	if(!IsDummyMiniViewTargetOutsideCurrentView(*GameClient(), Graphics(), MiniViewClientId))
		return false;

	const int MapW = GameClient()->Collision()->GetWidth();
	const int MapH = GameClient()->Collision()->GetHeight();
	if(MapW <= 0 || MapH <= 0)
		return false;

	const float SizeScale = g_Config.m_QmDummyMiniViewSize / 100.0f;
	const float MaxHeight = 80.0f * SizeScale;
	const float Margin = 5.0f;
	const float Aspect = m_Width / m_Height;

	H = MaxHeight;
	W = MaxHeight * Aspect;
	if(W > m_Width - Margin * 2.0f)
	{
		W = m_Width - Margin * 2.0f;
		H = W / Aspect;
	}

	X = m_Width - Margin - W;
	Y = Margin;

	return true;
}
void CHud::RenderDummyMiniMap()
{
	int BackendMajor = 0;
	int BackendMinor = 0;
	int BackendPatch = 0;
	const char *pBackendName = "";
	const auto Backend = Graphics()->GetDriverVersion(GRAPHICS_DRIVER_AGE_TYPE_DEFAULT, BackendMajor, BackendMinor, BackendPatch, pBackendName, BACKEND_TYPE_AUTO) ?
				     QmDummyMiniViewLayout::ResolveBackend(pBackendName) :
				     QmDummyMiniViewLayout::EBackend::UNKNOWN;
	if(m_DummyMiniViewGraphicsVersion != Graphics()->GraphicsResourcesResetVersion())
	{
		// 图形重建已经释放旧 GPU 资源，旧 ID 不能再提交销毁命令。
		m_DummyMiniViewRenderTarget.Invalidate();
		m_DummyMiniViewRenderTargetWidth = 0;
		m_DummyMiniViewRenderTargetHeight = 0;
		m_DummyMiniViewGraphicsVersion = Graphics()->GraphicsResourcesResetVersion();
	}
	float MiniX = 0.0f;
	float MiniY = 0.0f;
	float MiniW = 0.0f;
	float MiniH = 0.0f;
	if(!GetDummyMiniMapRect(MiniX, MiniY, MiniW, MiniH))
	{
		if(m_DummyMiniViewRenderTarget.IsValid() && !g_Config.m_QmDummyMiniView)
			DestroyDummyMiniViewRenderTarget();
		return;
	}

	const int DummyClientId = GameClient()->m_aLocalIds[1];
	const int MainClientId = GameClient()->m_aLocalIds[0];
	int MiniViewClientId = g_Config.m_ClDummy ? MainClientId : DummyClientId;
	if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
		MiniViewClientId = -1;

	SHudDummyMiniViewState ViewState = BuildHudDummyMiniViewState(*GameClient(), *Client(), false, DummyClientId, MiniViewClientId);
	if(!Graphics()->IsRenderTargetSupported())
	{
		DestroyDummyMiniViewRenderTarget();
		ViewState.m_HasSignal = false;
		str_copy(ViewState.m_aPlaceholderSubtitle, Localize("Graphics backend does not support dummy mini view"));
	}

	const float Radius = std::clamp(MiniH * 0.11f, 5.0f, 7.5f);
	DrawSmoothRoundedRect(Graphics(), MiniX + 0.8f, MiniY + 1.2f, MiniW, MiniH, Radius, ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL);
	DrawSmoothRoundedRect(Graphics(), MiniX, MiniY, MiniW, MiniH, Radius, ColorRGBA(0.02f, 0.03f, 0.05f, 0.92f), IGraphics::CORNER_ALL);
	DrawSmoothRoundedRect(Graphics(), MiniX + 0.75f, MiniY + 0.75f, std::max(0.0f, MiniW - 1.5f), std::max(0.0f, MiniH - 1.5f), std::max(0.0f, Radius - 0.55f), ViewState.m_TargetAccent.WithAlpha(0.16f), IGraphics::CORNER_ALL);

	const float FrameInset = 1.45f;
	const float FrameX = MiniX + FrameInset;
	const float FrameY = MiniY + FrameInset;
	const float FrameW = MiniW - FrameInset * 2.0f;
	const float FrameH = MiniH - FrameInset * 2.0f;
	const float FrameRadius = std::max(0.0f, Radius - 1.0f);
	DrawSmoothRoundedRect(Graphics(), FrameX, FrameY, FrameW, FrameH, FrameRadius, ColorRGBA(0.07f, 0.09f, 0.13f, 0.96f));
	DrawSmoothRoundedRect(Graphics(), FrameX + 0.65f, FrameY + 0.65f, std::max(0.0f, FrameW - 1.3f), std::max(0.0f, std::min(FrameH - 1.3f, FrameH * 0.48f)), std::max(0.0f, FrameRadius - 0.45f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f), IGraphics::CORNER_T);

	const float ContentInset = 1.9f;
	const float InnerX = FrameX + ContentInset;
	const float InnerY = FrameY + ContentInset;
	const float InnerW = FrameW - ContentInset * 2.0f;
	const float InnerH = FrameH - ContentInset * 2.0f;
	const float InnerRadius = std::max(0.0f, FrameRadius - 1.1f);
	if(InnerW <= 0.0f || InnerH <= 0.0f)
	{
		return;
	}

	DrawSmoothRoundedRect(Graphics(), InnerX, InnerY, InnerW, InnerH, InnerRadius, ColorRGBA(0.03f, 0.04f, 0.06f, 0.92f));

	if(ViewState.m_HasSignal && MiniViewClientId >= 0)
	{
		Graphics()->TextureClear();

		float SavedX0 = 0.0f;
		float SavedY0 = 0.0f;
		float SavedX1 = 0.0f;
		float SavedY1 = 0.0f;
		Graphics()->GetScreen(&SavedX0, &SavedY0, &SavedX1, &SavedY1);

		const int ScreenW = Graphics()->ScreenWidth();
		const int ScreenH = Graphics()->ScreenHeight();
		// 使用 HUD 编辑器变换后的投影计算尺寸；离屏目标保留完整内框，屏幕交集仅判断可见性。
		const auto Viewport = QmDummyMiniViewLayout::ResolveViewport(
			{InnerX, InnerY, InnerW, InnerH},
			{SavedX0, SavedY0, SavedX1 - SavedX0, SavedY1 - SavedY0}, ScreenW, ScreenH);
		const int ViewW = Viewport.IsVisible() ? std::max(1, (int)std::round(InnerW * ScreenW / (SavedX1 - SavedX0))) : 0;
		const int ViewH = Viewport.IsVisible() ? std::max(1, (int)std::round(InnerH * ScreenH / (SavedY1 - SavedY0))) : 0;
		const auto TargetSize = ResolveQmDummyMiniViewTargetSize(ViewW, ViewH,
			m_DummyMiniViewRenderTargetWidth, m_DummyMiniViewRenderTargetHeight, ScreenW, ScreenH);
		if(TargetSize.m_W > 0 && TargetSize.m_H > 0)
		{
			const ColorRGBA MiniClearColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClOverlayEntities ? g_Config.m_ClBackgroundEntitiesColor : g_Config.m_ClBackgroundColor));
			const bool TargetSizeChanged = TargetSize.m_W != m_DummyMiniViewRenderTargetWidth || TargetSize.m_H != m_DummyMiniViewRenderTargetHeight;
			if(TargetSizeChanged || !m_DummyMiniViewRenderTarget.IsValid())
			{
				DestroyDummyMiniViewRenderTarget();
				m_DummyMiniViewRenderTarget = Graphics()->CreateRenderTarget(TargetSize.m_W, TargetSize.m_H);
				if(m_DummyMiniViewRenderTarget.IsValid())
				{
					m_DummyMiniViewRenderTargetWidth = TargetSize.m_W;
					m_DummyMiniViewRenderTargetHeight = TargetSize.m_H;
				}
			}
			const bool RenderingMiniView = m_DummyMiniViewRenderTarget.IsValid() && Graphics()->BeginRenderTarget(m_DummyMiniViewRenderTarget, MiniClearColor);
			if(!RenderingMiniView)
			{
				ViewState.m_HasSignal = false;
				str_copy(ViewState.m_aPlaceholderSubtitle, Localize("Dummy mini view rendering unavailable"));
			}

			if(RenderingMiniView)
			{
				Graphics()->MapScreen(0.0f, 0.0f, 100.0f, 100.0f);
				Graphics()->DrawRect(0.0f, 0.0f, 100.0f, 100.0f, MiniClearColor, IGraphics::CORNER_NONE, 0.0f);

				const CGameClient::CClientData &MiniClient = GameClient()->m_aClients[MiniViewClientId];
				vec2 MiniPos(0.0f, 0.0f);
				bool HasSnapshotSignal = false;
				if(!TryGetDummyMiniViewTargetPos(*GameClient(), *Client(), MiniViewClientId, MiniPos, &HasSnapshotSignal))
				{
					Graphics()->EndRenderTarget();
					Graphics()->MapScreen(SavedX0, SavedY0, SavedX1, SavedY1);
					return;
				}

				const float ZoomScale = std::max(0.1f, g_Config.m_QmDummyMiniViewZoom / 100.0f);
				const float MiniZoom = GameClient()->m_Camera.m_Zoom * ZoomScale;

				bool RenderedBackground = false;
				GameClient()->m_AspectRatio.BeginCursorGameAspect();
				if(g_Config.m_ClOverlayEntities == 100)
					RenderedBackground = GameClient()->m_Background.RenderCustom(MiniPos, MiniZoom);
				if(!RenderedBackground)
					GameClient()->m_MapLayersBackground.RenderCustom(MiniPos, MiniZoom);

				Graphics()->MapScreen(Graphics()->MapScreenToWorld(MiniPos.x, MiniPos.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->GameScreenAspect(), MiniZoom));

				// 小窗只绘制已有状态，不新增特效或声音。
				const bool PrevMiniRender = GameClient()->IsRenderingDummyMiniMap();
				GameClient()->SetRenderingDummyMiniMap(true);

				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_PROJECTILE_TRAIL);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_TRAIL_EXTRA);
				GameClient()->m_Items.OnRender();
				GameClient()->m_Players.OnRender();
				GameClient()->m_MapLayersForeground.RenderCustom(MiniPos, MiniZoom);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_EXPLOSIONS);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_EXTRA);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_GENERAL);

				if(!HasSnapshotSignal)
				{
					CTeeRenderInfo TeeInfo = MiniClient.m_RenderInfo;
					const CAnimState *pIdleState = CAnimState::GetIdle();
					vec2 OffsetToMid;
					CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
					RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(MiniPos.x, MiniPos.y + OffsetToMid.y));
				}

				GameClient()->SetRenderingDummyMiniMap(PrevMiniRender);
				GameClient()->m_AspectRatio.EndCursorGameAspect();

				Graphics()->EndRenderTarget();
				Graphics()->MapScreen(SavedX0, SavedY0, SavedX1, SavedY1);

				IGraphics::SRenderTargetDrawParams DrawParams;
				DrawParams.m_X = InnerX;
				DrawParams.m_Y = InnerY;
				DrawParams.m_W = InnerW;
				DrawParams.m_H = InnerH;
				DrawParams.m_Corners = IGraphics::CORNER_ALL;
				DrawParams.m_Rounding = InnerRadius;
				DrawParams.m_Opaque = true;
				if(Backend == QmDummyMiniViewLayout::EBackend::VULKAN)
				{
					DrawParams.m_V0 = 0.0f;
					DrawParams.m_V1 = 1.0f;
				}
				Graphics()->DrawRenderTarget(m_DummyMiniViewRenderTarget, DrawParams);
			}
		}
	}

	if(!ViewState.m_HasSignal)
	{
		const unsigned int PrevFlags = TextRender()->GetRenderFlags();
		const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
		const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.36f);

		const float PlaceholderIconSize = std::clamp(std::min(InnerW, InnerH) * 0.14f, 7.0f, 11.0f);
		const float PlaceholderTitleSize = std::clamp(MiniH * 0.078f, 5.3f, 6.6f);
		const float PlaceholderBodySize = std::clamp(MiniH * 0.062f, 4.6f, 5.4f);
		const vec2 PlaceholderCenter(InnerX + InnerW * 0.5f, InnerY + InnerH * 0.52f);

		DrawSmoothCircle(Graphics(), vec2(PlaceholderCenter.x, PlaceholderCenter.y - 7.5f), PlaceholderIconSize * 0.9f, ViewState.m_TargetAccent.WithAlpha(0.16f));
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.68f);
		const float CameraWidth = PlaceholderIconSize;
		Ui()->DrawQmIconAt(PlaceholderCenter.x - CameraWidth * 0.5f, PlaceholderCenter.y - PlaceholderIconSize - 8.0f, PlaceholderIconSize, EQmIcon::CAMERA, FontIcon::CAMERA, TextRender()->GetTextColor());

		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.90f);
		const float TitleWidth = TextRender()->TextWidth(PlaceholderTitleSize, ViewState.m_aPlaceholderTitle);
		TextRender()->Text(PlaceholderCenter.x - TitleWidth * 0.5f, PlaceholderCenter.y - 3.0f, PlaceholderTitleSize, ViewState.m_aPlaceholderTitle, -1.0f);

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.64f);
		const float SubtitleMaxWidth = std::max(0.0f, InnerW - 16.0f);
		RenderHudEllipsizedText(TextRender(), PlaceholderCenter.x - SubtitleMaxWidth * 0.5f, PlaceholderCenter.y + 4.2f, PlaceholderBodySize, SubtitleMaxWidth, ViewState.m_aPlaceholderSubtitle);

		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->SetRenderFlags(PrevFlags);
		TextRender()->TextColor(PrevTextColor);
		TextRender()->TextOutlineColor(PrevOutlineColor);
	}

}
