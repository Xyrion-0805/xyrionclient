// 卡片内副标题共享绘制入口；悬浮只改变文字可见性，始终消费同一份卡片几何。
#ifndef GAME_CLIENT_QMUI_SETTINGSCARDSUBTITLE_H
#define GAME_CLIENT_QMUI_SETTINGSCARDSUBTITLE_H

#include "SettingsCardGeometry.h"
#include "SettingsPageLayout.h"
#include "UiContext.h"

#include <game/client/ui.h>

inline void RenderSettingsCardSubtitle(const IUiContext &Ctx, const SSettingsCardFrame &Frame, const SSettingsCardSpec &Spec, const bool PointerInside)
{
	if(Ctx.m_pUi == nullptr || Ctx.m_pUi->MeasurementOnly() || !PointerInside || Spec.m_pSubtitle == nullptr || Spec.m_pSubtitle[0] == '\0' || Frame.m_SubtitleRect.w <= 0.0f || Frame.m_SubtitleRect.h <= 0.0f)
		return;
	SLabelProperties Props;
	Props.m_MaxWidth = Frame.m_SubtitleRect.w;
	Props.m_EllipsisAtEnd = true;
	const float Scale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	Ctx.m_pUi->DoLabel(&Frame.m_SubtitleRect, Spec.m_pSubtitle, ResolveSettingsSmallFontSize(Scale), TEXTALIGN_ML, Props);
}

#endif
