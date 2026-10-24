// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef ENGINE_CLIENT_ROUNDED_RECT_DIRECTIONS_H
#define ENGINE_CLIENT_ROUNDED_RECT_DIRECTIONS_H

#include <base/dbg.h>
#include <base/vmath.h>

#include <array>

class CQmRoundedRectDirections
{
public:
	static constexpr int MIN_SEGMENTS = 8;
	static constexpr int MAX_SEGMENTS = 48;

private:
	static constexpr int NUM_QUALITIES = (MAX_SEGMENTS - MIN_SEGMENTS) / 2 + 1;
	std::array<std::array<vec2, MAX_SEGMENTS + 1>, NUM_QUALITIES> m_aaDirections{};
	std::array<bool, NUM_QUALITIES> m_aPrepared{};

public:
	// 仅由图形前端使用；按需准备每档角度，配置切换与固定档位回退互不失效。
	const vec2 *Get(int NumSegments)
	{
		dbg_assert(NumSegments >= MIN_SEGMENTS && NumSegments <= MAX_SEGMENTS && NumSegments % 2 == 0, "unsupported rounded rectangle segment count");
		const int Quality = (NumSegments - MIN_SEGMENTS) / 2;
		auto &aDirections = m_aaDirections[Quality];
		if(!m_aPrepared[Quality])
		{
			const float SegmentsAngle = pi / 2 / NumSegments;
			for(int i = 0; i <= NumSegments; ++i)
			{
				// 沿用原角度运算顺序，不将最后一个端点强行吸附到坐标轴。
				const float Angle = i * SegmentsAngle;
				aDirections[i] = vec2(std::cos(Angle), std::sin(Angle));
			}
			m_aPrepared[Quality] = true;
		}
		return aDirections.data();
	}
};

#endif
