#include "graphics.h"

bool IGraphics::CalculateGaussianBlurKernel(const SGaussianBlurParams &Params, std::array<float, GAUSSIAN_BLUR_MAX_RADIUS + 1> &aWeights)
{
	aWeights.fill(0.0f);
	if(Params.m_Radius < 1 || Params.m_Radius > GAUSSIAN_BLUR_MAX_RADIUS || !std::isfinite(Params.m_Sigma) || Params.m_Sigma <= 0.0f)
		return false;

	const double SigmaSquared = (double)Params.m_Sigma * Params.m_Sigma;
	double Sum = 0.0;
	for(int Offset = 0; Offset <= Params.m_Radius; ++Offset)
	{
		const double Weight = std::exp(-(double)(Offset * Offset) / (2.0 * SigmaSquared));
		aWeights[Offset] = (float)Weight;
		Sum += Offset == 0 ? Weight : Weight * 2.0;
	}
	if(!std::isfinite(Sum) || Sum <= 0.0)
	{
		aWeights.fill(0.0f);
		return false;
	}
	for(int Offset = 0; Offset <= Params.m_Radius; ++Offset)
		aWeights[Offset] = (float)(aWeights[Offset] / Sum);
	return true;
}

// helper functions
void IGraphics::CalcScreenParams(float Aspect, float Zoom, float *pWidth, float *pHeight) const
{
	const float Amount = 1150 * 1000;
	const float WMax = 1500;
	const float HMax = 1050;

	const float f = std::sqrt(Amount) / std::sqrt(Aspect);
	*pWidth = f * Aspect;
	*pHeight = f;

	// limit the view
	if(*pWidth > WMax)
	{
		*pWidth = WMax;
		*pHeight = *pWidth / Aspect;
	}

	if(*pHeight > HMax)
	{
		*pHeight = HMax;
		*pWidth = *pHeight * Aspect;
	}

	*pWidth *= Zoom;
	*pHeight *= Zoom;
}

CScreenRect IGraphics::MapScreenToWorld(float CenterX, float CenterY, float ParallaxX, float ParallaxY,
	float ParallaxZoom, float OffsetX, float OffsetY, float Aspect, float Zoom) const
{
	float Width, Height;
	CalcScreenParams(Aspect, Zoom, &Width, &Height);

	float Scale = (ParallaxZoom * (Zoom - 1.0f) + 100.0f) / 100.0f / Zoom;
	Width *= Scale;
	Height *= Scale;

	CenterX *= ParallaxX / 100.0f;
	CenterY *= ParallaxY / 100.0f;

	return CScreenRect(
		OffsetX + CenterX - Width / 2,
		OffsetY + CenterY - Height / 2,
		Width,
		Height);
}

void IGraphics::MapScreenToInterface(float CenterX, float CenterY, float Zoom)
{
	CScreenRect ScreenRect = MapScreenToWorld(CenterX, CenterY, 100.0f, 100.0f, 100.0f,
		0, 0, ScreenAspect(), Zoom);
	MapScreen(ScreenRect);
}

void IGraphics::MapScreenToSize(float Width, float Height)
{
	MapScreen(CScreenRect(0, 0, Width, Height));
}
