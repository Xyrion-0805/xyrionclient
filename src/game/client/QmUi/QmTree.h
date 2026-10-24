// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMTREE_H
#define GAME_CLIENT_QMUI_QMTREE_H

#include "QmAnimationBackend.h"

#include <game/client/ui_rect.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

struct SUiPresenceResult
{
	bool m_Render = false;
	float m_Alpha = 0.0f;
	bool m_FreshEnter = false;
};

class CUiV2Tree
{
public:
	void Reset();
	void BeginFrame();
	void TouchNode(uint64_t NodeKey);
	CUIRect ResolveLayoutTransition(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target, const struct SUiSpringConfig &Spring, int Priority = 1, bool Animate = true);
	CUIRect SyncLayoutTransition(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target);
	SUiPresenceResult ResolvePresence(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, bool Visible, const SUiAnimTransition &Transition);
	float ResolvePresenceAlpha(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const SUiAnimTransition &Transition);
	void EndFrame();
	void EndFrame(CUiV2AnimationRuntime &AnimRuntime);
	int NodeCount() const;

private:
	struct SLayoutTransitionState
	{
		CUIRect m_LastTarget{};
		bool m_HasLastTarget = false;
		uint64_t m_LastUseCounter = 0;
	};
	enum class EPresencePhase
	{
		ENTERING,
		PRESENT,
		EXITING,
	};
	struct SPresenceState
	{
		EPresencePhase m_Phase = EPresencePhase::ENTERING;
		bool m_TouchedThisFrame = false;
		uint64_t m_LastUseCounter = 0;
	};

	void PruneLayoutTransitionCache(uint64_t CurrentUseCounter);
	void ResolveEndFrame(CUiV2AnimationRuntime *pAnimRuntime);

	std::unordered_set<uint64_t> m_FrameNodeKeys;
	std::unordered_map<uint64_t, SLayoutTransitionState> m_LayoutTransitions;
	std::unordered_map<uint64_t, SPresenceState> m_PresenceStates;
	int m_LastNodeCount = 0;
	uint64_t m_LayoutUseCounter = 0;
	uint64_t m_PresenceUseCounter = 0;
};

#endif
