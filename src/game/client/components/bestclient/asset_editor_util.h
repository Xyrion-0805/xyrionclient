#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ASSET_EDITOR_UTIL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ASSET_EDITOR_UTIL_H

class CImageInfo;
namespace AssetsEditor
{
bool ValidExportName(const char *pName);
bool CopyScaled(CImageInfo &Dst, const CImageInfo &Src, int DstX, int DstY, int DstW, int DstH, int SrcX, int SrcY, int SrcW, int SrcH);
}
#endif
