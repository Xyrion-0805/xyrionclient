/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_ROUNDED_RECT_GEOMETRY_H
#define ENGINE_CLIENT_ROUNDED_RECT_GEOMETRY_H

#include <algorithm>
#include <cmath>

struct SRoundedRectGeometry
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_W = 0.0f;
	float m_H = 0.0f;
	float m_Rounding = 0.0f;
};

inline SRoundedRectGeometry ResolveRoundedRectGeometry(const float X, const float Y, const float W, const float H, const float Rounding, const float PixelSize)
{
	SRoundedRectGeometry Result{X, Y, W, H, 0.0f};
	if(W <= 0.0f || H <= 0.0f)
		return Result;

	const float ResolvedPixelSize = std::max(PixelSize, 0.0f);
	const auto AlignToPixel = [ResolvedPixelSize](const float Value) {
		return ResolvedPixelSize > 0.0f ? std::round(Value / ResolvedPixelSize) * ResolvedPixelSize : Value;
	};
	const float Right = AlignToPixel(X + W);
	const float Bottom = AlignToPixel(Y + H);
	Result.m_X = AlignToPixel(X);
	Result.m_Y = AlignToPixel(Y);
	Result.m_W = Right - Result.m_X;
	Result.m_H = Bottom - Result.m_Y;
	if(Result.m_W <= 0.0f || Result.m_H <= 0.0f)
	{
		Result.m_X = X;
		Result.m_Y = Y;
		Result.m_W = W;
		Result.m_H = H;
	}

	const float MaxRounding = std::max(0.0f, std::min(Result.m_W, Result.m_H) * 0.5f);
	Result.m_Rounding = std::clamp(AlignToPixel(Rounding), 0.0f, MaxRounding);
	return Result;
}

#endif
