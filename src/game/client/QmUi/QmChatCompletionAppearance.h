#ifndef GAME_CLIENT_QMUI_QMCHATCOMPLETIONAPPEARANCE_H
#define GAME_CLIENT_QMUI_QMCHATCOMPLETIONAPPEARANCE_H

#include "QmTheme.h"

#include <engine/shared/config.h>

#include <algorithm>

struct SQmChatCompletionAppearance
{
	ColorRGBA m_Background;
	float m_BackgroundOpacity;
	qm_theme::SImeTheme m_Theme;
};

inline SQmChatCompletionAppearance QmChatCompletionAppearance(const CConfig &Config)
{
	SQmChatCompletionAppearance Result;
	Result.m_BackgroundOpacity = std::clamp(Config.m_QmChatCompletionOpacity, 0, 100) / 100.0f;
	Result.m_Background = color_cast<ColorRGBA>(ColorHSLA(Config.m_QmChatCompletionBgColor)).WithAlpha(Result.m_BackgroundOpacity);
	// 布局沿用输入法主题，配色由补全卡片独立保存。
	Result.m_Theme = qm_theme::ImeTheme(true);
	Result.m_Theme.m_Text = color_cast<ColorRGBA>(ColorHSLA(Config.m_QmChatCompletionTextColor, true));
	Result.m_Theme.m_TextMuted = Result.m_Theme.m_Text.WithMultipliedAlpha(0.62f);
	Result.m_Theme.m_TextSelected = color_cast<ColorRGBA>(ColorHSLA(Config.m_QmChatCompletionSelectedTextColor, true));
	Result.m_Theme.m_SelectedBg = color_cast<ColorRGBA>(ColorHSLA(Config.m_QmChatCompletionSelectedColor, true));
	return Result;
}

#endif
