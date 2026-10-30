#ifndef GAME_EDITOR_QUAD_CROP_H
#define GAME_EDITOR_QUAD_CROP_H

#include <base/vmath.h>
#include <game/mapitems.h>

#include <array>
#include <vector>

// Map quad order: top-left, top-right, bottom-left, bottom-right.
std::array<vec2, 4> QuadCropRectangle(vec2 Start, vec2 End);
// Intersect the rendered triangles with the rectangle, preserving UVs, vertex
// colors, envelope references and the original animation pivot.
std::vector<CQuad> CropQuadToRectangle(const CQuad &Source, vec2 Start, vec2 End);

#endif
