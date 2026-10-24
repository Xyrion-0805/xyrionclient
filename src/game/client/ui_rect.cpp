/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui_rect.h"

#include "ui.h"

#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/QmUi/UiSurface.h>

#include <algorithm>
#include <cmath>

IGraphics *CUIRect::ms_pGraphics = nullptr;
CUi *CUIRect::ms_pUi = nullptr;

void CUIRect::DrawRectBackdrop(int Corners, float Rounding) const
{
	if(ms_pUi != nullptr && ms_pUi->GaussianBlurScopeActive())
		ms_pUi->RenderGaussianBlur(*this, ms_pUi->GaussianBlurScopeAlpha(), Corners, Rounding);
}

namespace
{
	float CurrentPixelSize(IGraphics *pGraphics)
	{
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const int ScreenWidth = pGraphics->ScreenWidth();
		return ScreenWidth > 0 ? std::max(std::abs(ScreenX1 - ScreenX0) / ScreenWidth, 0.0001f) : 0.0001f;
	}
}

void CUIRect::Draw(ColorRGBA Color, int Corners, float Rounding) const
{
	if(ms_pUi != nullptr && ms_pUi->DrawingSuppressed())
		return;
	if(Color.a > 0.0f && Color.a < 1.0f)
		DrawRectBackdrop(Corners, Rounding);
	if(ms_pGraphics->HasRoundedRectSdf() && Rounding > 0.0f && Corners != IGraphics::CORNER_NONE)
	{
		const float PixelSize = CurrentPixelSize(ms_pGraphics);
		SRoundedSurfaceParams Params;
		Params.m_Radius = Rounding;
		Params.m_PixelSize = PixelSize;
		Params.m_Corners = Corners;
		if(DrawRoundedSurface(ms_pGraphics, *this, Color, ColorRGBA(), Params))
			return;
	}
	ms_pGraphics->DrawRect(x, y, w, h, Color, Corners, Rounding);
}

void CUIRect::Draw4(ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, int Corners, float Rounding) const
{
	if(ms_pUi != nullptr && ms_pUi->DrawingSuppressed())
		return;
	const float MaximumAlpha = std::max({ColorTopLeft.a, ColorTopRight.a, ColorBottomLeft.a, ColorBottomRight.a});
	if(MaximumAlpha > 0.0f && (ColorTopLeft.a < 1.0f || ColorTopRight.a < 1.0f || ColorBottomLeft.a < 1.0f || ColorBottomRight.a < 1.0f))
		DrawRectBackdrop(Corners, Rounding);
	ms_pGraphics->DrawRect4(x, y, w, h, ColorTopLeft, ColorTopRight, ColorBottomLeft, ColorBottomRight, Corners, Rounding);
}

void CUIRect::DrawOutline(ColorRGBA Color) const
{
	if(ms_pUi != nullptr && ms_pUi->DrawingSuppressed())
		return;
	const IGraphics::CLineItem aArray[] = {
		IGraphics::CLineItem(x, y, x + w, y),
		IGraphics::CLineItem(x + w, y, x + w, y + h),
		IGraphics::CLineItem(x + w, y + h, x, y + h),
		IGraphics::CLineItem(x, y + h, x, y)};
	ms_pGraphics->TextureClear();
	ms_pGraphics->LinesBegin();
	ms_pGraphics->SetColor(Color);
	ms_pGraphics->LinesDraw(aArray, std::size(aArray));
	ms_pGraphics->LinesEnd();
}
