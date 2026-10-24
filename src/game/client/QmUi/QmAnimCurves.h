// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMANIMCURVES_H
#define GAME_CLIENT_QMUI_QMANIMCURVES_H

#include "QmAnim.h"

// Named UI curve / spring presets, intended for consumption by feat-003 design
// tokens and downstream page-level features. All entries are inline constexpr
// so they incur zero overhead at the use site.

namespace ui_curve
{
	inline constexpr SUiAnimTransition STANDARD = {
		.m_DurationSec = 0.30f,
		.m_Easing = EEasing::EASE_IN_OUT_CUBIC,
	};

	inline constexpr SUiAnimTransition EMPHASIZED = {
		.m_DurationSec = 0.50f,
		.m_Easing = EEasing::CUBIC_BEZIER,
		.m_Bezier = {0.2f, 0.0f, 0.0f, 1.0f},
	};

	inline constexpr SUiAnimTransition DECELERATE = {
		.m_DurationSec = 0.25f,
		.m_Easing = EEasing::EASE_OUT_QUART,
	};

	inline constexpr SUiAnimTransition ACCELERATE = {
		.m_DurationSec = 0.20f,
		.m_Easing = EEasing::EASE_IN,
	};

	inline constexpr SUiAnimTransition BOUNCE_OUT = {
		.m_DurationSec = 0.35f,
		.m_Easing = EEasing::EASE_OUT_BACK,
	};
} // namespace ui_curve

namespace ui_spring
{
	inline constexpr SUiSpringConfig GENTLE = {1.0f, 120.0f, 14.0f};
	inline constexpr SUiSpringConfig SNAPPY = {1.0f, 280.0f, 26.0f};
	inline constexpr SUiSpringConfig WOBBLY = {1.0f, 180.0f, 12.0f};
} // namespace ui_spring

#endif
