/* Copyright © 2026 BestProject Team */
#include "inputs.h"

#include <base/math.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace
{
float ModeOffsetTicks(int Mode, int Amount, float Scale)
{
	return g_Config.m_BcInputs == Mode && Amount > 0 ? Amount / Scale : 0.0f;
}
} // namespace

float BcInputs::EffectiveOffsetTicks()
{
	if(g_Config.m_XcPixelMove || g_Config.m_XcTeeBalanceRunning) return 0.0f;
	switch(g_Config.m_BcInputs)
	{
	case BC_INPUTS_FAST: return ModeOffsetTicks(BC_INPUTS_FAST, g_Config.m_TcFastInputAmount, 20.0f);
	case BC_INPUTS_BEST:
	{
		float Offset = ModeOffsetTicks(BC_INPUTS_BEST, g_Config.m_BcBestInputAmount, 100.0f);
		Offset *= 1.0f - g_Config.m_BcBestInputSmoothing / 200.0f;
		Offset *= 1.0f + g_Config.m_BcBestInputLatencyComp / 100.0f;
		return Offset;
	}
	case BC_INPUTS_SAIKO: return ModeOffsetTicks(BC_INPUTS_SAIKO, g_Config.m_BcSaikoInputAmount, 100.0f);
	case BC_INPUTS_DELTA: return ModeOffsetTicks(BC_INPUTS_DELTA, g_Config.m_BcDeltaInputAmount, 100.0f);
	case BC_INPUTS_F: return ModeOffsetTicks(BC_INPUTS_F, g_Config.m_BcFInputAmount, 1000.0f);
	default: return 0.0f;
	}
}

int BcInputs::PredictionTicks(float OffsetTicks) { return OffsetTicks > 0.0f ? (int)std::ceil(OffsetTicks) : 0; }
int BcInputs::PredictionTicksOthers(float OffsetTicks) { return PredictionTicks(OffsetTicks); }

void BcInputs::ApplyOffset(float OffsetTicks, int &Tick, float &Intra)
{
	if(OffsetTicks <= 0.0f) return;
	const float Combined = Intra + OffsetTicks - (int)OffsetTicks;
	const int Carry = (int)Combined;
	Tick += (int)OffsetTicks + Carry;
	Intra = Combined - Carry;
}

float BcInputs::BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable)
{
	if(!Enable) return Fraction;
	const float T = std::clamp(Fraction, 0.0f, 1.0f);
	const float Cubic = T * T * (3.0f - 2.0f * T);
	switch(std::clamp(g_Config.m_BcBestInputInterpolation, 1, 3))
	{
	case 2: return Cubic;
	case 3: return mix(T, Cubic, std::clamp(DeltaLength / 1000.0f, 0.0f, 1.0f));
	default: return T;
	}
}

vec2 BcInputs::BestInterpolate(vec2 Prev, vec2 Cur, float Fraction, bool Enable)
{
	return mix(Prev, Cur, BestInterpolationAmount(Fraction, length(Cur - Prev), Enable));
}
float BcInputs::BestInterpolate(float Prev, float Cur, float Fraction, bool Enable)
{
	return mix(Prev, Cur, BestInterpolationAmount(Fraction, absolute(Cur - Prev), Enable));
}

bool BcInputs::FastOthers() { return g_Config.m_BcInputs == BC_INPUTS_FAST && g_Config.m_TcFastInputOthers; }
bool BcInputs::BestOthers() { return g_Config.m_BcInputs == BC_INPUTS_BEST && g_Config.m_BcBestInputOthers; }
bool BcInputs::SaikoOthers() { return g_Config.m_BcInputs == BC_INPUTS_SAIKO && g_Config.m_BcSaikoInputOthers; }
bool BcInputs::DeltaOthers() { return g_Config.m_BcInputs == BC_INPUTS_DELTA && g_Config.m_BcDeltaInputOthers; }
bool BcInputs::FOthers() { return g_Config.m_BcInputs == BC_INPUTS_F && g_Config.m_BcFInputOthers; }
bool BcInputs::CloudOthers() { return g_Config.m_BcInputs == BC_INPUTS_CLOUD && g_Config.m_BcCloudInputOthers; }
bool BcInputs::AnyOthers() { return FastOthers() || BestOthers() || SaikoOthers() || DeltaOthers() || FOthers() || CloudOthers(); }
bool BcInputs::ImmediateOthers() { return BestOthers() || SaikoOthers() || DeltaOthers(); }
