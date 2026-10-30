// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMMOTION_H
#define GAME_CLIENT_QMUI_QMMOTION_H

#include "QmAnimationBackend.h"

namespace qm_motion
{
	inline int NormalizeMotionLevel(int MotionLevel)
	{
		return qm_animation::NormalizeMotionLevel(MotionLevel);
	}

	inline SUiAnimTransition ApplyMotionLevel(SUiAnimTransition Transition, int MotionLevel)
	{
		return qm_animation::ApplyMotionLevel(Transition, MotionLevel);
	}
} // namespace qm_motion

#endif
