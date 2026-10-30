#include <game/editor/quad_crop.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
CQuad Square()
{
	CQuad Q{};
	Q.m_aPoints[0] = {0, 0};
	Q.m_aPoints[1] = {102400, 0};
	Q.m_aPoints[2] = {0, 102400};
	Q.m_aPoints[3] = {102400, 102400};
	Q.m_aPoints[4] = {47000, 39000};
	Q.m_aTexcoords[0] = {0, 0};
	Q.m_aTexcoords[1] = {1024, 0};
	Q.m_aTexcoords[2] = {0, 1024};
	Q.m_aTexcoords[3] = {1024, 1024};
	for(auto &Color : Q.m_aColors) Color = {70, 130, 210, 180};
	Q.m_PosEnv = 2; Q.m_PosEnvOffset = 300;
	Q.m_ColorEnv = 1; Q.m_ColorEnvOffset = 400;
	return Q;
}

double Area(const std::vector<CQuad> &vQuads)
{
	double Total = 0;
	for(const auto &Q : vQuads)
		for(const auto &T : {std::array<int, 3>{0, 1, 3}, std::array<int, 3>{0, 3, 2}})
		{
			const auto A = Q.m_aPoints[T[0]], B = Q.m_aPoints[T[1]], C = Q.m_aPoints[T[2]];
			Total += std::abs(((double)B.x - A.x) * ((double)C.y - A.y) - ((double)B.y - A.y) * ((double)C.x - A.x)) / (2 * 1024 * 1024);
		}
	return Total;
}
}

TEST(QuadCrop, DragDirectionsCompleteTheOtherTwoCorners)
{
	for(const auto &Start : {vec2(25, 10), vec2(75, 10), vec2(25, 80), vec2(75, 80)})
	{
		const vec2 End(100 - Start.x, 90 - Start.y);
		const auto Corners = QuadCropRectangle(Start, End);
		EXPECT_EQ(Corners[0], vec2(25, 10));
		EXPECT_EQ(Corners[1], vec2(75, 10));
		EXPECT_EQ(Corners[2], vec2(25, 80));
		EXPECT_EQ(Corners[3], vec2(75, 80));
		const auto Result = CropQuadToRectangle(Square(), Start, End);
		ASSERT_EQ(Result.size(), 1u);
		EXPECT_DOUBLE_EQ(Area(Result), 3500);
		EXPECT_EQ(Result[0].m_aPoints[0].x, 25600);
		EXPECT_EQ(Result[0].m_aPoints[3].y, 81920);
	}
}

TEST(QuadCrop, TextureAndAnimationStayAttachedToSource)
{
	const CQuad Source = Square();
	const auto Result = CropQuadToRectangle(Source, vec2(25, 10), vec2(75, 80));
	ASSERT_EQ(Result.size(), 1u);
	const auto &Q = Result[0];
	EXPECT_EQ(Q.m_aTexcoords[0].x, 256);
	EXPECT_EQ(Q.m_aTexcoords[0].y, 102);
	EXPECT_EQ(Q.m_aTexcoords[3].x, 768);
	EXPECT_EQ(Q.m_aTexcoords[3].y, 819);
	EXPECT_EQ(Q.m_aColors[0].r, 70);
	EXPECT_EQ(Q.m_aColors[3].a, 180);
	EXPECT_EQ(Q.m_aPoints[4].x, Source.m_aPoints[4].x);
	EXPECT_EQ(Q.m_aPoints[4].y, Source.m_aPoints[4].y);
	EXPECT_EQ(Q.m_PosEnv, Source.m_PosEnv);
	EXPECT_EQ(Q.m_PosEnvOffset, Source.m_PosEnvOffset);
	EXPECT_EQ(Q.m_ColorEnv, Source.m_ColorEnv);
	EXPECT_EQ(Q.m_ColorEnvOffset, Source.m_ColorEnvOffset);
	EXPECT_EQ(Source.m_aPoints[0].x, 0);
}

TEST(QuadCrop, PartiallyOutsideAndRotatedQuadOnlyKeepIntersection)
{
	CQuad Q = Square();
	Q.m_aPoints[0] = {102400, 0};
	Q.m_aPoints[1] = {204800, 102400};
	Q.m_aPoints[2] = {0, 102400};
	Q.m_aPoints[3] = {102400, 204800};
	const auto Result = CropQuadToRectangle(Q, vec2(-50, 50), vec2(100, 150));
	ASSERT_FALSE(Result.empty());
	EXPECT_NEAR(Area(Result), 7500, .001);
	for(const auto &Part : Result)
		for(int I = 0; I < 4; ++I)
		{
			const double X = Part.m_aPoints[I].x / 1024.0, Y = Part.m_aPoints[I].y / 1024.0;
			EXPECT_GE(X, 0); EXPECT_LE(X, 100);
			EXPECT_GE(Y, 50); EXPECT_LE(Y, 150);
			EXPECT_LE(std::abs(X - 100) + std::abs(Y - 100), 100.001);
		}
}

TEST(QuadCrop, NonAffineColorsKeepOriginalTriangleInterpolation)
{
	CQuad Q = Square();
	Q.m_aColors[0].r = Q.m_aColors[3].r = 0;
	Q.m_aColors[1].r = Q.m_aColors[2].r = 255;
	const auto Result = CropQuadToRectangle(Q, vec2(25, 10), vec2(75, 80));
	ASSERT_GT(Result.size(), 1u);
	EXPECT_NEAR(Area(Result), 3500, .001);
	for(const auto &Part : Result)
		for(int I = 0; I < 4; ++I)
			EXPECT_NEAR(Part.m_aColors[I].r, std::abs(Part.m_aPoints[I].x - Part.m_aPoints[I].y) / 1024.0 * 2.55, .501);
}

TEST(QuadCrop, EmptyZeroAreaDegenerateAndNonFiniteSelections)
{
	CQuad Q = Square();
	EXPECT_TRUE(CropQuadToRectangle(Q, vec2(110, 0), vec2(150, 50)).empty());
	EXPECT_TRUE(CropQuadToRectangle(Q, vec2(100, 0), vec2(150, 50)).empty());
	EXPECT_TRUE(CropQuadToRectangle(Q, vec2(10, 10), vec2(10, 90)).empty());
	EXPECT_TRUE(CropQuadToRectangle(Q, vec2(10, 10), vec2(90, 10)).empty());
	EXPECT_TRUE(CropQuadToRectangle(Q, vec2(10, 10), vec2(std::numeric_limits<float>::infinity(), 90)).empty());
	for(auto &Point : Q.m_aPoints) Point = {0, 0};
	EXPECT_TRUE(CropQuadToRectangle(Q, vec2(-10, -10), vec2(10, 10)).empty());
}

TEST(QuadCrop, FullSelectionKeepsQuadAndMirroredTextureWorks)
{
	CQuad Q = Square();
	const auto Full = CropQuadToRectangle(Q, vec2(-10, -10), vec2(110, 110));
	ASSERT_EQ(Full.size(), 1u);
	EXPECT_DOUBLE_EQ(Area(Full), 10000);
	EXPECT_EQ(Full[0].m_aPoints[4].x, Q.m_aPoints[4].x);
	for(int I = 0; I < 4; ++I) Q.m_aPoints[I].x = 102400 - Q.m_aPoints[I].x;
	const auto Mirror = CropQuadToRectangle(Q, vec2(25, 10), vec2(75, 80));
	ASSERT_EQ(Mirror.size(), 1u);
	EXPECT_EQ(Mirror[0].m_aTexcoords[0].x, 768);
	EXPECT_EQ(Mirror[0].m_aTexcoords[3].x, 256);
}
