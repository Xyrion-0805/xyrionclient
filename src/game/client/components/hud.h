/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_HUD_H
#define GAME_CLIENT_COMPONENTS_HUD_H
#include <engine/client.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include "hud_media_island_logic.h"
#include "system_media_controls.h"
#include "tclient/swap_countdown_message.h"

struct SScoreInfo
{
	SScoreInfo()
	{
		Reset();
	}

	void Reset()
	{
		m_TextRankContainerIndex.Reset();
		m_TextScoreContainerIndex.Reset();
		m_RoundRectQuadContainerIndex = -1;
		m_OptionalNameTextContainerIndex.Reset();
		m_aScoreText[0] = 0;
		m_aRankText[0] = 0;
		m_aPlayerNameText[0] = 0;
		m_ScoreTextWidth = 0.f;
		m_Initialized = false;
	}

	STextContainerIndex m_TextRankContainerIndex;
	STextContainerIndex m_TextScoreContainerIndex;
	float m_ScoreTextWidth;
	char m_aScoreText[16];
	char m_aRankText[16];
	char m_aPlayerNameText[MAX_NAME_LENGTH];
	int m_RoundRectQuadContainerIndex;
	STextContainerIndex m_OptionalNameTextContainerIndex;

	bool m_Initialized;
};

class CHud : public CComponent
{
	float m_Width = 0, m_Height = 0;
	struct SHudMediaIslandAnimState
	{
		static constexpr int SATELLITE_MAX_ITEMS = IGraphics::MEDIA_ISLAND_SDF_MAX_ITEMS;
		static constexpr int SATELLITE_LIVE_MAX_ITEMS = SATELLITE_MAX_ITEMS;

		enum class EVisualState
		{
			MINIMIZED,
			EXPANDED,
		};

		EVisualState m_VisualState = EVisualState::MINIMIZED;
		int64_t m_ExpandUntilTick = 0;
		int64_t m_TrackDetailsUntilTick = 0;
		bool m_LyricsActive = false;
		bool m_LyricsMarqueeInitialized = false;
		int64_t m_LyricsMarqueeStartTick = 0;
		char m_aLyricsMarquee[256] = {};
		float m_TargetX = 0.0f;
		float m_TargetWidth = 0.0f;
		float m_TargetHeight = 0.0f;
		float m_TargetTitleAlpha = 0.0f;
		float m_TargetTitleOffset = 0.0f;
		float m_TargetSpectatorAlpha = 0.0f;
		float m_TargetBottomAlpha = 0.0f;
		float m_TargetCoverInAlpha = 1.0f;
		float m_TargetCoverOutAlpha = 0.0f;
		float m_TargetCoverInScale = 1.0f;
		float m_TargetCoverOutScale = 1.0f;
		float m_TargetTrackTitleInAlpha = 1.0f;
		float m_TargetTrackTitleOutAlpha = 0.0f;
		float m_TargetTrackTitleInOffset = 0.0f;
		float m_TargetTrackTitleOutOffset = 0.0f;
		float m_TargetTrackMetaInAlpha = 1.0f;
		float m_TargetTrackMetaOutAlpha = 0.0f;
		float m_TargetTrackMetaInOffset = 0.0f;
		float m_TargetTrackMetaOutOffset = 0.0f;
		bool m_WaveformWasPlaying = false;
		bool m_WaveformSettling = false;
		int64_t m_WaveformSettleStartTick = 0;
		float m_WaveformSettleSampleTime = 0.0f;
		bool m_LayoutInitialized = false;
		bool m_HasTrackIdentity = false;
		SHudMediaIslandTrackSnapshot m_CurrentTrack;
		SHudMediaIslandTrackSnapshot m_OutgoingTrack;
		bool m_TrackTransitionActive = false;
		bool m_TrackTransitionNeedsNodeReset = false;
		int64_t m_TrackTransitionStartTick = 0;
		float m_OldTrackExitProgress = 1.0f;
		float m_NewTrackEnterProgress = 1.0f;
		bool m_CapsuleMorphActive = false;
		bool m_CapsuleMorphNeedsCapture = false;
		float m_CapsuleMorphFromX = 0.0f;
		float m_CapsuleMorphFromWidth = 0.0f;
		float m_CapsuleMorphFromHeight = 0.0f;
		struct SSatelliteItem
		{
			bool m_Used = false;
			bool m_Active = false;
			bool m_Seen = false;
			bool m_NodeInitialized = false;
			bool m_Completed = false;
			bool m_SwapOutgoing = false;
			EHudMediaIslandCountdownType m_Type = EHudMediaIslandCountdownType::SWAP;
			int m_Id = 0;
			int64_t m_TriggerTick = 0;
			int64_t m_ExitStartTick = 0;
			float m_Progress = 0.0f;
			// 分离弹簧：内部是位移/速度，朝目标（分离 1、收回 0）收敛。
			SHudMediaIslandBlobSpring m_LiquidSpring;
			int64_t m_LiquidLastTick = 0;
			float m_LiquidOriginCenterX = 0.0f;
			float m_LiquidOriginWidth = 0.0f;

			void Reset()
			{
				*this = {};
			}
		};
		std::array<SSatelliteItem, SATELLITE_MAX_ITEMS> m_aSatelliteItems{};
		bool m_SatelliteGroupInitialized = false;
		float m_TargetSatelliteX = 0.0f;
		float m_TargetSatelliteWidth = 0.0f;
		float m_TargetSatelliteAlpha = 0.0f;
		SHudMediaIslandBlobSpring m_SpectatorLiquidSpring;
		int64_t m_SpectatorLiquidLastTick = 0;
		int m_SpectatorDisplayCount = 0;
		float m_SpectatorIconProgress = 1.0f;
		int64_t m_SpectatorIconLastTick = 0;
		bool m_SpectatorHadWatchers = false;
		float m_SpectatorExitLiquidStart = 0.0f;
		float m_SpectatorExitIconStart = 1.0f;

		void StartCapsuleMorph()
		{
			m_CapsuleMorphActive = true;
			m_CapsuleMorphNeedsCapture = true;
		}

		// 左侧倒计时副岛包括仍在收回的项目。
		bool HasVisibleCountdownSatellite() const
		{
			for(const SSatelliteItem &Item : m_aSatelliteItems)
			{
				if(Item.m_Used)
					return true;
			}
			return false;
		}

		bool HasVisibleSatellite() const
		{
			// 观战卫星长在主岛右侧，与左侧倒计时副岛一起算作副岛。
			return m_SpectatorLiquidSpring.m_Progress > 0.0f || HasVisibleCountdownSatellite();
		}

		void Reset()
		{
			m_VisualState = EVisualState::MINIMIZED;
			m_ExpandUntilTick = 0;
			m_TrackDetailsUntilTick = 0;
			m_LyricsActive = false;
			m_LyricsMarqueeInitialized = false;
			m_LyricsMarqueeStartTick = 0;
			m_aLyricsMarquee[0] = '\0';
			m_TargetX = 0.0f;
			m_TargetWidth = 0.0f;
			m_TargetHeight = 0.0f;
			m_TargetTitleAlpha = 0.0f;
			m_TargetTitleOffset = 0.0f;
			m_TargetSpectatorAlpha = 0.0f;
			m_TargetBottomAlpha = 0.0f;
			m_TargetCoverInAlpha = 1.0f;
			m_TargetCoverOutAlpha = 0.0f;
			m_TargetCoverInScale = 1.0f;
			m_TargetCoverOutScale = 1.0f;
			m_TargetTrackTitleInAlpha = 1.0f;
			m_TargetTrackTitleOutAlpha = 0.0f;
			m_TargetTrackTitleInOffset = 0.0f;
			m_TargetTrackTitleOutOffset = 0.0f;
			m_TargetTrackMetaInAlpha = 1.0f;
			m_TargetTrackMetaOutAlpha = 0.0f;
			m_TargetTrackMetaInOffset = 0.0f;
			m_TargetTrackMetaOutOffset = 0.0f;
			m_WaveformWasPlaying = false;
			m_WaveformSettling = false;
			m_WaveformSettleStartTick = 0;
			m_WaveformSettleSampleTime = 0.0f;
			m_LayoutInitialized = false;
			m_HasTrackIdentity = false;
			m_CurrentTrack.Reset();
			m_OutgoingTrack.Reset();
			m_TrackTransitionActive = false;
			m_TrackTransitionNeedsNodeReset = false;
			m_TrackTransitionStartTick = 0;
			m_OldTrackExitProgress = 1.0f;
			m_NewTrackEnterProgress = 1.0f;
			m_CapsuleMorphActive = false;
			m_CapsuleMorphNeedsCapture = false;
			m_CapsuleMorphFromX = 0.0f;
			m_CapsuleMorphFromWidth = 0.0f;
			m_CapsuleMorphFromHeight = 0.0f;
			for(SSatelliteItem &Item : m_aSatelliteItems)
				Item.Reset();
			m_SatelliteGroupInitialized = false;
			m_TargetSatelliteX = 0.0f;
			m_TargetSatelliteWidth = 0.0f;
			m_TargetSatelliteAlpha = 0.0f;
			m_SpectatorLiquidSpring = {};
			m_SpectatorLiquidLastTick = 0;
			m_SpectatorDisplayCount = 0;
			m_SpectatorIconProgress = 1.0f;
			m_SpectatorIconLastTick = 0;
			m_SpectatorHadWatchers = false;
			m_SpectatorExitLiquidStart = 0.0f;
			m_SpectatorExitIconStart = 1.0f;
		}
	};
	SHudMediaIslandAnimState m_MediaIslandAnimState;
	struct SHudMediaIslandFrameCache
	{
		uint64_t m_Frame = 0;
		bool m_Valid = false;
		bool m_HasMediaState = false;
		CSystemMediaControls::SState m_MediaState{};
		bool m_ShowLyrics = false;
		bool m_LyricsActive = false;
		char m_aLyrics[256] = {};
		ColorRGBA m_LyricsColor = ColorRGBA(0.97f, 0.98f, 1.0f, 0.90f);
		int m_SpectatorCount = 0;
		bool m_HasVisible = false;
		bool m_AvoidanceValid = false;
		float m_AvoidanceRight = 0.0f;

		void Reset()
		{
			*this = {};
			m_LyricsColor = ColorRGBA(0.97f, 0.98f, 1.0f, 0.90f);
		}
	};
	mutable SHudMediaIslandFrameCache m_MediaIslandFrameCache;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurSource;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurDownsample;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurDownsampleTemporary;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurDownsampleTarget;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurTarget;
	int m_MediaIslandBlurWidth = 0;
	int m_MediaIslandBlurHeight = 0;
	int m_MediaIslandBlurMode = -1;
	bool m_MediaIslandBlurReady = false;
	uint64_t m_MediaIslandBlurLastAttemptFrame = 0;
	bool m_MediaIslandBlurAttemptInitialized = false;
	struct SHudRecordingStatusAnimState
	{
		float m_TargetWidth = 0.0f;
		float m_TargetAlpha = 0.0f;
		float m_TargetTextAlpha = 0.0f;
		bool m_Initialized = false;

		void Reset()
		{
			m_TargetWidth = 0.0f;
			m_TargetAlpha = 0.0f;
			m_TargetTextAlpha = 0.0f;
			m_Initialized = false;
		}
	};
	SHudRecordingStatusAnimState m_RecordingStatusAnimState;
	struct SHudMediaIslandMuteState
	{
		bool m_Confirmed = false;
		int64_t m_TriggerTick = 0;
		int64_t m_EndTick = 0;
		int64_t m_DurationTicks = 0;

		void Reset()
		{
			*this = {};
		}
	};
	SHudMediaIslandMuteState m_MediaIslandMuteState;
	static constexpr int SWITCH_COUNTDOWN_MAX_LINES = 3;
	SHudSwitchCountdownTracker m_SwitchCountdownTracker;
	CSwapCountdownTracker m_aIslandSwapTrackers[NUM_DUMMIES];
	void EnsureMediaIslandFrameCache() const;
	void DestroyMediaIslandBlurTargets();
	IGraphics::CRenderTargetHandle MediaIslandBlurBackdrop();
	void RenderMediaIsland();
	void HandleSpamProtectionMessage(const char *pMessage);
	void UpdateSwitchCountdownTracker();
	bool HasActiveSwitchCountdown() const;
	CUIRect m_MediaIslandLastVisibleRect{};
	bool m_MediaIslandLastVisibleRectValid = false;


	int m_HudQuadContainerIndex;
	SScoreInfo m_aScoreInfo[2];
	STextContainerIndex m_FPSTextContainerIndex;
	STextContainerIndex m_DDRaceEffectsTextContainerIndex;
	STextContainerIndex m_PlayerAngleTextContainerIndex;
	float m_PlayerPrevAngle;
	STextContainerIndex m_aPlayerSpeedTextContainers[2];
	float m_aPlayerPrevSpeed[2];
	int m_aPlayerSpeed[2];
	enum class ESpeedChange
	{
		NONE,
		INCREASE,
		DECREASE
	};
	ESpeedChange m_aLastPlayerSpeedChange[2];
	STextContainerIndex m_aPlayerPositionContainers[2];
	float m_aPlayerPrevPosition[2];

	IGraphics::CRenderTargetHandle m_DummyMiniViewRenderTarget;
	int m_DummyMiniViewRenderTargetWidth = 0;
	int m_DummyMiniViewRenderTargetHeight = 0;
	unsigned m_DummyMiniViewGraphicsVersion = 0;
	void DestroyDummyMiniViewRenderTarget();
	bool GetDummyMiniMapRect(float &X, float &Y, float &W, float &H) const;
	void RenderDummyMiniMap();
	void RenderCursor();

	void RenderTextInfo();
	void RenderConnectionWarning();
	void RenderTeambalanceWarning();

	void PrepareAmmoHealthAndArmorQuads();
	void RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter);

	void PreparePlayerStateQuads();
	void RenderPlayerState(int ClientId);

	int m_LastSpectatorCountTick;
	void RenderSpectatorCount();
	void RenderDummyActions();
	void RenderMovementInformation();

	void UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, float &PrevValue);
	void RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y);

	class CMovementInformation
	{
	public:
		vec2 m_Pos;
		vec2 m_Speed;
		float m_Angle = 0.0f;
	};
	class CMovementInformation GetMovementInformation(int ClientId, int Conn) const;

	void RenderGameTimer();
	void RenderPauseNotification();
	void RenderSuddenDeath();

	void RenderScoreHud();
	int m_LastLocalClientId = -1;

	void RenderSpectatorHud();
	void RenderWarmupTimer();
	void RenderLocalTime(float x);

	static constexpr float MOVEMENT_INFORMATION_LINE_HEIGHT = 8.0f;

public:
	CHud();
	bool HasVisibleMediaIsland() const;
	bool ReferencesMediaIslandTexture(IGraphics::CTextureHandle Texture) const;
	void ReleaseMediaIslandTracks();
	vec2 GetMediaIslandPushOffset(const CUIRect &Rect, float CanvasWidth, float CanvasHeight, float Padding) const;
	const std::vector<SSwapCountdownState> &MediaIslandSwaps(int Dummy) const { return m_aIslandSwapTrackers[Dummy].Entries(); }
	float GetPlayerRecordSeconds(int Dummy) const { return m_aPlayerRecord[Dummy]; }
	int Sizeof() const override { return sizeof(*this); }

	void ResetHudContainers();
	void OnWindowResize() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnRender() override;
	void OnInit() override;
	void OnNewSnapshot() override;

	// DDRace

	void OnMessage(int MsgType, void *pRawMsg) override;
	void RenderNinjaBarPos(float x, float y, float Width, float Height, float Progress, float Alpha = 1.0f);

private:
	void RenderRecord();
	void RenderDDRaceEffects();
	float m_TimeCpDiff;
	float m_aPlayerRecord[NUM_DUMMIES];
	float m_FinishTimeDiff;
	int m_DDRaceTime;
	int m_FinishTimeLastReceivedTick;
	int m_TimeCpLastReceivedTick;
	bool m_ShowFinishTime;

	inline float GetMovementInformationBoxHeight();
	inline int GetDigitsIndex(int Value, int Max);

	// Quad Offsets
	int m_aAmmoOffset[NUM_WEAPONS];
	int m_HealthOffset;
	int m_EmptyHealthOffset;
	int m_ArmorOffset;
	int m_EmptyArmorOffset;
	int m_aCursorOffset[NUM_WEAPONS];
	int m_FlagOffset;
	int m_AirjumpOffset;
	int m_AirjumpEmptyOffset;
	int m_aWeaponOffset[NUM_WEAPONS];
	int m_EndlessJumpOffset;
	int m_EndlessHookOffset;
	int m_JetpackOffset;
	int m_TeleportGrenadeOffset;
	int m_TeleportGunOffset;
	int m_TeleportLaserOffset;
	int m_SoloOffset;
	int m_CollisionDisabledOffset;
	int m_HookHitDisabledOffset;
	int m_HammerHitDisabledOffset;
	int m_GunHitDisabledOffset;
	int m_ShotgunHitDisabledOffset;
	int m_GrenadeHitDisabledOffset;
	int m_LaserHitDisabledOffset;
	int m_DeepFrozenOffset;
	int m_LiveFrozenOffset;
	int m_DummyHammerOffset;
	int m_DummyCopyOffset;
	int m_PracticeModeOffset;
	int m_Team0ModeOffset;
	int m_LockModeOffset;
};

#endif
