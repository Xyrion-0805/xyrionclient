#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TEE_ACCESSORIES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TEE_ACCESSORIES_H
#include <base/vmath.h>
class IGraphics;
class CAnimState;
class CTeeRenderInfo;
void RenderTeeAccessories(IGraphics *pGraphics, const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Direction, vec2 Position, float Alpha);
#endif
