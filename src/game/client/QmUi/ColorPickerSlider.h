#ifndef GAME_CLIENT_QMUI_COLORPICKERSLIDER_H
#define GAME_CLIENT_QMUI_COLORPICKERSLIDER_H

#include <algorithm>

// 取色器通道滑条（H/S/V/A、R/G/B、H/S/L）几何：
// 轨道横向铺满整行留给滑条的宽度，滑块圆心行程两端各内缩一个基础半径，
// 使值为 0 / 255 时滑块完整落在轨道内，而不是悬在轨道端外或压到标签与数值框。
// 命中换算与圆心行程同源，点击轨道上任意位置得到的值与滑块落点一致。
namespace qm_picker_slider
{
	// 滑块基础半径（未悬停/未拖拽）与悬停放大半径，单位 ui 逻辑像素。
	// 圆心行程只使用基础半径，悬停放大不会让滑块左右跳动。
	inline constexpr float KNOB_RADIUS = 4.8f;
	inline constexpr float KNOB_HOVER_RADIUS = 5.5f;

	// 圆心可移动的宽度：轨道两端各留出一个基础半径。
	inline float KnobTravel(float TrackWidth)
	{
		return std::max(0.0f, TrackWidth - KNOB_RADIUS * 2.0f);
	}

	// 归一化值 [0,1] -> 圆心相对轨道左边界的偏移，落在 [KNOB_RADIUS, TrackWidth - KNOB_RADIUS]。
	inline float KnobCenterOffset(float TrackWidth, float Normalized)
	{
		return KNOB_RADIUS + KnobTravel(TrackWidth) * std::clamp(Normalized, 0.0f, 1.0f);
	}

	// 轨道内命中点（相对轨道左边界的偏移）-> 归一化值；两端各一个半径的区间分别归到 0 与 1。
	inline float ValueFromTrackOffset(float TrackWidth, float TrackOffset)
	{
		const float Travel = KnobTravel(TrackWidth);
		if(Travel <= 0.0f)
			return 0.0f;
		return std::clamp((TrackOffset - KNOB_RADIUS) / Travel, 0.0f, 1.0f);
	}
} // namespace qm_picker_slider

#endif
