/* Copyright © 2026 BestProject Team */
#include "asset_editor_util.h"
#include <base/str.h>
#include <engine/image.h>
#include <algorithm>
#include <cstring>

bool AssetsEditor::ValidExportName(const char *pName)
{
	if(!pName || !*pName || !str_utf8_check(pName) || str_comp_nocase(pName, "default") == 0 || str_find(pName, ".."))
		return false;
	const size_t Length = std::strlen(pName);
	if(Length >= 50 || pName[0] == ' ' || pName[Length - 1] == '.' || pName[Length - 1] == ' ')
		return false;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pName); *p; ++p)
		if(*p < 32 || *p == 127 || std::strchr("\\/:*?\"<>|", *p))
			return false;
	char aStem[64];
	str_copy(aStem, pName);
	if(char *pDot = std::strchr(aStem, '.')) *pDot = '\0';
	for(const char *pReserved : {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"})
		if(str_comp_nocase(aStem, pReserved) == 0) return false;
	return true;
}

bool AssetsEditor::CopyScaled(CImageInfo &Dst, const CImageInfo &Src, int DstX, int DstY, int DstW, int DstH, int SrcX, int SrcY, int SrcW, int SrcH)
{
	if(!Dst.m_pData || !Src.m_pData || Dst.m_Format != CImageInfo::FORMAT_RGBA || Src.m_Format != CImageInfo::FORMAT_RGBA)
		return false;
	if(DstW <= 0 || DstH <= 0 || SrcW <= 0 || SrcH <= 0 || DstX < 0 || DstY < 0 || SrcX < 0 || SrcY < 0)
		return false;
	if(size_t(DstX) > Dst.m_Width || size_t(DstW) > Dst.m_Width - DstX || size_t(DstY) > Dst.m_Height || size_t(DstH) > Dst.m_Height - DstY ||
		size_t(SrcX) > Src.m_Width || size_t(SrcW) > Src.m_Width - SrcX || size_t(SrcY) > Src.m_Height || size_t(SrcH) > Src.m_Height - SrcY)
		return false;
	// A self-copy may overlap: sample immutable source bytes, just like separate donor/result images.
	CImageInfo Copy;
	const CImageInfo *pSource = &Src;
	if(Dst.m_pData == Src.m_pData) { Copy = Src.DeepCopy(); pSource = &Copy; }
	for(int Y = 0; Y < DstH; ++Y)
	{
		const size_t SampleY = SrcY + (int64_t(Y) * SrcH) / DstH;
		for(int X = 0; X < DstW; ++X)
		{
			const size_t SampleX = SrcX + (int64_t(X) * SrcW) / DstW;
			const size_t DstOffset = ((size_t(DstY) + Y) * Dst.m_Width + DstX + X) * 4;
			const size_t SrcOffset = (SampleY * Src.m_Width + SampleX) * 4;
			std::copy_n(pSource->m_pData + SrcOffset, 4, Dst.m_pData + DstOffset);
		}
	}
	Copy.Free();
	return true;
}
