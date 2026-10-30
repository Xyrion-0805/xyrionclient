#include "quad_crop.h"

#include <algorithm>
#include <cmath>

namespace
{
struct CVertex
{
	double m_X, m_Y;
	std::array<double, 6> m_aAttributes; // U, V, R, G, B, A
};

double Cross(const CVertex &A, const CVertex &B, const CVertex &C)
{
	return (B.m_X - A.m_X) * (C.m_Y - A.m_Y) - (B.m_Y - A.m_Y) * (C.m_X - A.m_X);
}

bool Sample(const CVertex &A, const CVertex &B, const CVertex &C, CVertex &Point, bool InsideOnly)
{
	const double Area = Cross(A, B, C);
	if(std::abs(Area) < 0.5) return false;
	const double WA = Cross(Point, B, C) / Area;
	const double WB = Cross(A, Point, C) / Area;
	const double WC = 1 - WA - WB;
	if(InsideOnly && (WA < -1e-9 || WB < -1e-9 || WC < -1e-9)) return false;
	for(size_t I = 0; I < Point.m_aAttributes.size(); ++I)
		Point.m_aAttributes[I] = A.m_aAttributes[I] * WA + B.m_aAttributes[I] * WB + C.m_aAttributes[I] * WC;
	return true;
}

std::vector<CVertex> Clip(const std::vector<CVertex> &Polygon, int Axis, double Bound, bool KeepGreater)
{
	std::vector<CVertex> Result;
	if(Polygon.empty()) return Result;
	const auto Coordinate = [Axis](const CVertex &V) { return Axis == 0 ? V.m_X : V.m_Y; };
	const auto Inside = [&](const CVertex &V) { return KeepGreater ? Coordinate(V) >= Bound : Coordinate(V) <= Bound; };
	CVertex Previous = Polygon.back();
	bool PreviousInside = Inside(Previous);
	for(const auto &Current : Polygon)
	{
		const bool CurrentInside = Inside(Current);
		if(CurrentInside != PreviousInside)
		{
			const double T = std::clamp((Bound - Coordinate(Previous)) / (Coordinate(Current) - Coordinate(Previous)), 0.0, 1.0);
			CVertex Intersection;
			Intersection.m_X = Previous.m_X + (Current.m_X - Previous.m_X) * T;
			Intersection.m_Y = Previous.m_Y + (Current.m_Y - Previous.m_Y) * T;
			if(Axis == 0) Intersection.m_X = Bound; else Intersection.m_Y = Bound;
			for(size_t I = 0; I < Intersection.m_aAttributes.size(); ++I)
				Intersection.m_aAttributes[I] = Previous.m_aAttributes[I] + (Current.m_aAttributes[I] - Previous.m_aAttributes[I]) * T;
			Result.push_back(Intersection);
		}
		if(CurrentInside) Result.push_back(Current);
		Previous = Current;
		PreviousInside = CurrentInside;
	}
	// Clipping through an existing corner can produce consecutive duplicates.
	Result.erase(std::unique(Result.begin(), Result.end(), [](const CVertex &A, const CVertex &B) { return std::abs(A.m_X - B.m_X) < 1e-6 && std::abs(A.m_Y - B.m_Y) < 1e-6; }), Result.end());
	if(Result.size() > 1 && std::abs(Result.front().m_X - Result.back().m_X) < 1e-6 && std::abs(Result.front().m_Y - Result.back().m_Y) < 1e-6)
		Result.pop_back();
	return Result;
}

void Append(std::vector<CQuad> &Result, const CQuad &Source, const std::array<CVertex, 4> &Vertices)
{
	CQuad Quad = Source;
	for(int I = 0; I < 4; ++I)
	{
		Quad.m_aPoints[I] = {(int)std::round(Vertices[I].m_X), (int)std::round(Vertices[I].m_Y)};
		Quad.m_aTexcoords[I] = {(int)std::round(Vertices[I].m_aAttributes[0]), (int)std::round(Vertices[I].m_aAttributes[1])};
		Quad.m_aColors[I] = {
			(int)std::round(std::clamp(Vertices[I].m_aAttributes[2], 0.0, 255.0)),
			(int)std::round(std::clamp(Vertices[I].m_aAttributes[3], 0.0, 255.0)),
			(int)std::round(std::clamp(Vertices[I].m_aAttributes[4], 0.0, 255.0)),
			(int)std::round(std::clamp(Vertices[I].m_aAttributes[5], 0.0, 255.0))};
	}
	const auto Area = [&](int A, int B, int C) {
		return ((double)Quad.m_aPoints[B].x - Quad.m_aPoints[A].x) * ((double)Quad.m_aPoints[C].y - Quad.m_aPoints[A].y) -
			((double)Quad.m_aPoints[B].y - Quad.m_aPoints[A].y) * ((double)Quad.m_aPoints[C].x - Quad.m_aPoints[A].x);
	};
	if(std::abs(Area(0, 1, 3)) + std::abs(Area(0, 3, 2)) > 0)
		Result.push_back(Quad);
}
}

std::array<vec2, 4> QuadCropRectangle(vec2 Start, vec2 End)
{
	const vec2 Min(std::min(Start.x, End.x), std::min(Start.y, End.y));
	const vec2 Max(std::max(Start.x, End.x), std::max(Start.y, End.y));
	return {Min, vec2(Max.x, Min.y), vec2(Min.x, Max.y), Max};
}

std::vector<CQuad> CropQuadToRectangle(const CQuad &Source, vec2 Start, vec2 End)
{
	std::vector<CQuad> Result;
	if(!std::isfinite(Start.x) || !std::isfinite(Start.y) || !std::isfinite(End.x) || !std::isfinite(End.y)) return Result;
	// Work in fixed-point source units, rounding each rectangle edge once.
	double Left = std::round(std::min(Start.x, End.x) * 1024.0);
	double Right = std::round(std::max(Start.x, End.x) * 1024.0);
	double Top = std::round(std::min(Start.y, End.y) * 1024.0);
	double Bottom = std::round(std::max(Start.y, End.y) * 1024.0);
	if(Left >= Right || Top >= Bottom) return Result;
	std::array<CVertex, 4> V;
	bool AllInside = true;
	for(int I = 0; I < 4; ++I)
	{
		V[I] = {(double)Source.m_aPoints[I].x, (double)Source.m_aPoints[I].y,
			{(double)Source.m_aTexcoords[I].x, (double)Source.m_aTexcoords[I].y,
				(double)Source.m_aColors[I].r, (double)Source.m_aColors[I].g, (double)Source.m_aColors[I].b, (double)Source.m_aColors[I].a}};
		AllInside &= V[I].m_X >= Left && V[I].m_X <= Right && V[I].m_Y >= Top && V[I].m_Y <= Bottom;
	}
	const double Area1 = Cross(V[0], V[1], V[3]), Area2 = Cross(V[0], V[3], V[2]);
	if(std::abs(Area1) < 0.5 && std::abs(Area2) < 0.5) return Result;
	if(AllInside) return {Source};
	Left = std::max(Left, std::min({V[0].m_X, V[1].m_X, V[2].m_X, V[3].m_X}));
	Right = std::min(Right, std::max({V[0].m_X, V[1].m_X, V[2].m_X, V[3].m_X}));
	Top = std::max(Top, std::min({V[0].m_Y, V[1].m_Y, V[2].m_Y, V[3].m_Y}));
	Bottom = std::min(Bottom, std::max({V[0].m_Y, V[1].m_Y, V[2].m_Y, V[3].m_Y}));
	if(Left >= Right || Top >= Bottom) return Result;

	// Common case: an affine textured quad yields a single rectangular quad.
	// Otherwise keep the original triangle boundary so colors/UVs do not warp.
	constexpr int aOrder[] = {0, 1, 3, 2};
	bool Convex = true;
	for(int I = 0; I < 4; ++I)
		Convex &= Cross(V[aOrder[I]], V[aOrder[(I + 1) % 4]], V[aOrder[(I + 2) % 4]]) * Area1 > 0;
	CVertex Probe = V[2];
	bool Affine = Sample(V[0], V[1], V[3], Probe, false);
	for(size_t I = 0; I < Probe.m_aAttributes.size(); ++I)
		Affine &= std::abs(Probe.m_aAttributes[I] - V[2].m_aAttributes[I]) < 1e-6;
	if(Convex && Affine)
	{
		std::array<CVertex, 4> Corners = {CVertex{Left, Top, {}}, CVertex{Right, Top, {}}, CVertex{Left, Bottom, {}}, CVertex{Right, Bottom, {}}};
		bool Inside = true;
		for(auto &Corner : Corners)
			Inside &= Sample(V[0], V[1], V[3], Corner, true) || Sample(V[0], V[3], V[2], Corner, true);
		if(Inside) { Append(Result, Source, Corners); return Result; }
	}
	for(const auto &Triangle : {std::array<int, 3>{0, 1, 3}, std::array<int, 3>{0, 3, 2}})
	{
		if(std::abs(Cross(V[Triangle[0]], V[Triangle[1]], V[Triangle[2]])) < 0.5) continue;
		std::vector<CVertex> Polygon = {V[Triangle[0]], V[Triangle[1]], V[Triangle[2]]};
		Polygon = Clip(Polygon, 0, Left, true);
		Polygon = Clip(Polygon, 0, Right, false);
		Polygon = Clip(Polygon, 1, Top, true);
		Polygon = Clip(Polygon, 1, Bottom, false);
		if(Polygon.size() == 4)
			Append(Result, Source, {Polygon[0], Polygon[1], Polygon[3], Polygon[2]});
		else
			for(size_t I = 1; I + 1 < Polygon.size(); ++I)
				Append(Result, Source, {Polygon[0], Polygon[I], Polygon[I + 1], Polygon[I + 1]});
	}
	return Result;
}
