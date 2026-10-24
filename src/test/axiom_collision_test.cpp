#include <engine/map.h>
#include <engine/shared/config.h>

#include <game/axiom_quad_collision.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/mapitems_ex.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
class CQuadTestMap : public IMap
{
public:
	struct CItem
	{
		int m_Type;
		int m_Id;
		std::vector<int> m_vWords;
	};
	std::vector<CItem> m_vItems;
	std::vector<std::vector<unsigned char>> m_vData;

	void AddItem(int Type, int Id, int Size, const void *pData)
	{
		CItem Item{Type, Id, std::vector<int>(Size / sizeof(int))};
		std::memcpy(Item.m_vWords.data(), pData, Size);
		m_vItems.push_back(std::move(Item));
		std::stable_sort(m_vItems.begin(), m_vItems.end(), [](const auto &A, const auto &B) { return A.m_Type < B.m_Type; });
	}
	template<class T>
	void AddItem(int Type, int Id, const T &Item)
	{
		AddItem(Type, Id, sizeof(T), &Item);
	}
	int AddData(int Size, const void *pData)
	{
		m_vData.emplace_back(Size);
		std::memcpy(m_vData.back().data(), pData, Size);
		return m_vData.size() - 1;
	}
	int GetDataSize(int Index) const override { return Index >= 0 && Index < NumData() ? m_vData[Index].size() : 0; }
	void *GetData(int Index) override { return Index >= 0 && Index < NumData() ? m_vData[Index].data() : nullptr; }
	void *GetDataSwapped(int Index) override { return GetData(Index); }
	const char *GetDataString(int) override { return ""; }
	void UnloadData(int) override {}
	int NumData() const override { return m_vData.size(); }
	int GetItemSize(int Index) override { return Index >= 0 && Index < NumItems() ? m_vItems[Index].m_vWords.size() * sizeof(int) : 0; }
	void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr, CUuid * = nullptr) override
	{
		if(Index < 0 || Index >= NumItems())
			return nullptr;
		if(pType)
			*pType = m_vItems[Index].m_Type;
		if(pId)
			*pId = m_vItems[Index].m_Id;
		return m_vItems[Index].m_vWords.data();
	}
	void GetType(int Type, int *pStart, int *pNum) override
	{
		*pStart = 0;
		*pNum = 0;
		for(size_t i = 0; i < m_vItems.size(); ++i)
			if(m_vItems[i].m_Type == Type)
			{
				if(*pNum == 0)
					*pStart = i;
				++*pNum;
			}
	}
	int FindItemIndex(int Type, int Id) override
	{
		for(size_t i = 0; i < m_vItems.size(); ++i)
			if(m_vItems[i].m_Type == Type && m_vItems[i].m_Id == Id)
				return i;
		return -1;
	}
	void *FindItem(int Type, int Id) override { return GetItem(FindItemIndex(Type, Id)); }
	int NumItems() const override { return m_vItems.size(); }
	bool Load(const char *, IStorage *, const char *, int) override { return false; }
	bool Load(IStorage *, const char *, int) override { return false; }
	void Unload() override {}
	bool IsLoaded() const override { return true; }
	IOHANDLE File() const override { return nullptr; }
	const char *FullName() const override { return "quad-test"; }
	const char *BaseName() const override { return FullName(); }
	const char *Path() const override { return FullName(); }
	SHA256_DIGEST Sha256() const override { return {}; }
	unsigned Crc() const override { return 0; }
	int Size() const override { return 0; }
};

CQuad Rect(float X, float Y, float W, float H)
{
	CQuad Quad{};
	Quad.m_aPoints[0] = {f2fx(X), f2fx(Y)};
	Quad.m_aPoints[1] = {f2fx(X + W), f2fx(Y)};
	Quad.m_aPoints[2] = {f2fx(X), f2fx(Y + H)};
	Quad.m_aPoints[3] = {f2fx(X + W), f2fx(Y + H)};
	Quad.m_aPoints[4] = {f2fx(X + W / 2), f2fx(Y + H / 2)};
	for(auto &Color : Quad.m_aColors)
		Color = {255, 255, 255, 255};
	Quad.m_PosEnv = Quad.m_ColorEnv = -1;
	return Quad;
}

class CAxiomCollisionTest : public ::testing::Test
{
protected:
	CQuadTestMap m_Map;
	CLayers m_Layers;
	CCollision m_Collision;
	CMapItemGroup m_QuadGroup{};
	int m_LayerCount = 1;

	CAxiomCollisionTest()
	{
		CMapItemGroup GameGroup{};
		GameGroup.m_Version = 3;
		GameGroup.m_ParallaxX = GameGroup.m_ParallaxY = 100;
		GameGroup.m_NumLayers = 1;
		m_Map.AddItem(MAPITEMTYPE_GROUP, 0, GameGroup);
		m_QuadGroup = GameGroup;
		m_QuadGroup.m_StartLayer = 1;
		std::vector<CTile> vTiles(32 * 32);
		CMapItemLayerTilemap GameLayer{};
		GameLayer.m_Layer.m_Type = LAYERTYPE_TILES;
		GameLayer.m_Version = 3;
		GameLayer.m_Width = GameLayer.m_Height = 32;
		GameLayer.m_Flags = TILESLAYERFLAG_GAME;
		GameLayer.m_Data = m_Map.AddData(vTiles.size() * sizeof(CTile), vTiles.data());
		m_Map.AddItem(MAPITEMTYPE_LAYER, 0, GameLayer);
	}
	void AddQuad(const char *pName, CQuad Quad)
	{
		CMapItemLayerQuads Layer{};
		Layer.m_Layer.m_Type = LAYERTYPE_QUADS;
		Layer.m_Version = 2;
		Layer.m_NumQuads = 1;
		Layer.m_Image = -1;
		Layer.m_Data = m_Map.AddData(sizeof(Quad), &Quad);
		StrToInts(Layer.m_aName, std::size(Layer.m_aName), pName);
		m_Map.AddItem(MAPITEMTYPE_LAYER, m_LayerCount++, Layer);
	}
	void Init()
	{
		m_QuadGroup.m_NumLayers = m_LayerCount - 1;
		m_Map.AddItem(MAPITEMTYPE_GROUP, 1, m_QuadGroup);
		m_Layers.Init(&m_Map, false, false);
		m_Collision.Init(&m_Layers);
	}
	void Envelope(int Channels, std::vector<CEnvPoint> vPoints)
	{
		CMapItemEnvelope Env{};
		Env.m_Version = 2;
		Env.m_Synchronized = 1;
		Env.m_Channels = Channels;
		Env.m_NumPoints = vPoints.size();
		m_Map.AddItem(MAPITEMTYPE_ENVELOPE, 0, Env);
		m_Map.AddItem(MAPITEMTYPE_ENVPOINTS, 0, vPoints.size() * sizeof(CEnvPoint), vPoints.data());
	}
	void BezierEnvelope(bool Upstream)
	{
		CMapItemEnvelope Env{};
		Env.m_Version = Upstream ? 3 : 2;
		Env.m_Synchronized = 1;
		Env.m_Channels = 3;
		Env.m_NumPoints = 2;
		m_Map.AddItem(MAPITEMTYPE_ENVELOPE, 0, Env);
		CEnvPointBezier_upstream aPoints[2]{};
		aPoints[0].m_Curvetype = CURVETYPE_BEZIER;
		aPoints[1].m_Time = CFixedTime(1000);
		aPoints[1].m_aValues[0] = f2fx(100);
		aPoints[0].m_Bezier.m_aOutTangentDeltaX[0] = CFixedTime(333);
		aPoints[1].m_Bezier.m_aInTangentDeltaX[0] = CFixedTime(-333);
		if(Upstream)
			m_Map.AddItem(MAPITEMTYPE_ENVPOINTS, 0, sizeof(aPoints), aPoints);
		else
		{
			CEnvPoint aRegular[2] = {aPoints[0], aPoints[1]};
			CEnvPointBezier aBezier[2] = {aPoints[0].m_Bezier, aPoints[1].m_Bezier};
			m_Map.AddItem(MAPITEMTYPE_ENVPOINTS, 0, sizeof(aRegular), aRegular);
			m_Map.AddItem(MAPITEMTYPE_ENVPOINTS_BEZIER, 0, sizeof(aBezier), aBezier);
		}
	}
};
} // namespace

TEST_F(CAxiomCollisionTest, DefaultOffAndIndependentInstances)
{
	AddQuad("QHook", Rect(100, 100, 100, 100));
	Init();
	EXPECT_FALSE(m_Collision.CheckPoint(150, 150));
	CCollision Server;
	Server.Init(&m_Layers);
	const int Previous = g_Config.m_XcAxiomQuadCollision;
	g_Config.m_XcAxiomQuadCollision = 1;
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_TRUE(m_Collision.CheckPoint(150, 150));
	EXPECT_FALSE(Server.CheckPoint(150, 150));
	m_Collision.SetAxiomQuadCollision(false);
	EXPECT_FALSE(m_Collision.CheckPoint(150, 150));
	g_Config.m_XcAxiomQuadCollision = Previous;
}

TEST_F(CAxiomCollisionTest, HookNoHookAndNearestIntersection)
{
	AddQuad("QHook", Rect(150, 100, 30, 50));
	AddQuad("QStopa", Rect(100, 100, 0.25f, 50));
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	vec2 Hit, Before;
	int Tele = -1;
	EXPECT_EQ(m_Collision.IntersectLineTeleHook(vec2(0, 125), vec2(200, 125), &Hit, &Before, &Tele), TILE_NOHOOK);
	EXPECT_NEAR(Hit.x, 100, 0.001f);
	EXPECT_LT(Before.x, Hit.x);
	EXPECT_EQ(Tele, 0);
	EXPECT_EQ(m_Collision.IntersectLineTeleHook(vec2(130, 125), vec2(200, 125), &Hit, nullptr), TILE_SOLID);
	EXPECT_NEAR(Hit.x, 150, 0.001f);
	EXPECT_EQ(m_Collision.IntersectLineTeleWeapon(vec2(0, 125), vec2(200, 125), &Hit, nullptr), TILE_NOHOOK);
}

TEST_F(CAxiomCollisionTest, ThinQuadAndBoxWithoutCornerInside)
{
	AddQuad("QHook", Rect(100, 100, 0.25f, 50));
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_TRUE(m_Collision.TestBox(vec2(100, 125), vec2(28, 28)));
	EXPECT_FALSE(m_Collision.TestBox(vec2(85, 125), vec2(28, 28)));
	vec2 Pos(50, 125), Vel(100, 0);
	m_Collision.MoveBox(&Pos, &Vel, vec2(28, 28), vec2(0, 0));
	EXPECT_LE(Pos.x, 86.01f);
	EXPECT_FLOAT_EQ(Vel.x, 0);
}

TEST_F(CAxiomCollisionTest, StationaryOverlapAndContainmentPushout)
{
	AddQuad("QHook", Rect(100, 100, 100, 100));
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	vec2 Pos(150, 150), Vel(0, 0);
	m_Collision.MoveBox(&Pos, &Vel, vec2(28, 28), vec2(0, 0));
	EXPECT_FALSE(m_Collision.TestBox(Pos, vec2(28, 28)));
	EXPECT_GE(distance(Pos, vec2(150, 150)), 63.99f);
}

TEST_F(CAxiomCollisionTest, PointBounceOnThinQuad)
{
	AddQuad("QHook", Rect(100, 100, 0.25f, 50));
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	vec2 Pos(50, 125), Vel(100, 0);
	int Bounces = 0;
	m_Collision.MovePoint(&Pos, &Vel, 1, &Bounces);
	EXPECT_LT(Pos.x, 100);
	EXPECT_FLOAT_EQ(Vel.x, -100);
	EXPECT_EQ(Bounces, 1);
}

TEST_F(CAxiomCollisionTest, PolygonEdgesDoNotBecomeAnAabb)
{
	CQuad Quad = Rect(100, 100, 100, 100);
	Quad.m_aPoints[0] = {f2fx(150), f2fx(100)};
	Quad.m_aPoints[1] = {f2fx(200), f2fx(150)};
	Quad.m_aPoints[2] = {f2fx(100), f2fx(150)};
	Quad.m_aPoints[3] = {f2fx(150), f2fx(200)};
	AddQuad("QHook", Quad);
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_FALSE(m_Collision.CheckPoint(105, 105));
	EXPECT_TRUE(m_Collision.CheckPoint(150, 100));
	EXPECT_TRUE(m_Collision.CheckPoint(150, 150));
	EXPECT_FALSE(m_Collision.TestBox(vec2(105, 105), vec2(10, 10)));
}

TEST_F(CAxiomCollisionTest, TickTranslationRotationHookAndRewind)
{
	CQuad Quad = Rect(100, 100, 100, 20);
	Quad.m_PosEnv = 0;
	AddQuad("QHook", Quad);
	CEnvPoint A{}, B{};
	A.m_Curvetype = CURVETYPE_LINEAR;
	B.m_Time = CFixedTime(1000);
	B.m_aValues[0] = f2fx(100);
	B.m_aValues[2] = f2fx(90);
	Envelope(3, {A, B});
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	m_Collision.SetAxiomQuadRoundStartTick(100);
	m_Collision.SetAxiomQuadTick(100, 50);
	int Anchor = -1;
	vec2 Local, Hook;
	ASSERT_TRUE(m_Collision.FindAxiomHookAnchor(vec2(100, 110), &Anchor, &Local));
	m_Collision.SetAxiomQuadTick(125, 50);
	ASSERT_TRUE(m_Collision.GetAxiomHookAnchor(Anchor, Local, &Hook));
	EXPECT_NEAR(Hook.x, 200 - 50 / std::sqrt(2.0f), 0.01f);
	EXPECT_NEAR(Hook.y, 110 - 50 / std::sqrt(2.0f), 0.01f);
	EXPECT_TRUE(m_Collision.CheckPoint(200, 110));
	EXPECT_FALSE(m_Collision.CheckPoint(100, 110));
	m_Collision.SetAxiomQuadTick(100, 50);
	ASSERT_TRUE(m_Collision.GetAxiomHookAnchor(Anchor, Local, &Hook));
	EXPECT_NEAR(Hook.x, 100, 0.01f);
	EXPECT_NEAR(Hook.y, 110, 0.01f);
	m_Collision.SetAxiomQuadTick(150, 50);
	EXPECT_TRUE(m_Collision.CheckPoint(100, 110)); // loop boundary
}

TEST_F(CAxiomCollisionTest, AnimatedTransparentQuadReleasesHook)
{
	CQuad Quad = Rect(100, 100, 100, 100);
	Quad.m_ColorEnv = 0;
	AddQuad("QHook", Quad);
	CEnvPoint A{}, B{}, C{};
	A.m_Curvetype = B.m_Curvetype = CURVETYPE_STEP;
	A.m_aValues[3] = f2fx(1);
	B.m_Time = CFixedTime(500);
	C.m_Time = CFixedTime(1000);
	C.m_aValues[3] = f2fx(1);
	Envelope(4, {A, B, C});
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	int Anchor;
	vec2 Local, Hook;
	ASSERT_TRUE(m_Collision.FindAxiomHookAnchor(vec2(100, 150), &Anchor, &Local));
	m_Collision.SetAxiomQuadTick(30, 50);
	EXPECT_FALSE(m_Collision.CheckPoint(150, 150));
	EXPECT_FALSE(m_Collision.GetAxiomHookAnchor(Anchor, Local, &Hook));
	m_Collision.SetAxiomQuadTick(0, 50);
	EXPECT_TRUE(m_Collision.CheckPoint(150, 150));
}

TEST_F(CAxiomCollisionTest, NegativeEnvelopeOffsetAndZeroDuration)
{
	CQuad Quad = Rect(100, 100, 100, 100);
	Quad.m_PosEnv = 0;
	Quad.m_PosEnvOffset = -500;
	AddQuad("QHook", Quad);
	CEnvPoint A{}, B{};
	A.m_Curvetype = CURVETYPE_LINEAR;
	B.m_Time = CFixedTime(1000);
	B.m_aValues[0] = f2fx(100);
	Envelope(3, {A, B});
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_TRUE(m_Collision.CheckPoint(240, 150));
	EXPECT_FALSE(m_Collision.CheckPoint(110, 150));
}

TEST_F(CAxiomCollisionTest, GroupClippingAppliesToRaysAndMovement)
{
	AddQuad("QHook", Rect(100, 100, 100, 100));
	m_QuadGroup.m_UseClipping = 1;
	m_QuadGroup.m_ClipX = 150;
	m_QuadGroup.m_ClipY = 100;
	m_QuadGroup.m_ClipW = 50;
	m_QuadGroup.m_ClipH = 100;
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_FALSE(m_Collision.CheckPoint(125, 150));
	EXPECT_TRUE(m_Collision.CheckPoint(175, 150));
	EXPECT_FALSE(m_Collision.TestBox(vec2(125, 150), vec2(10, 10)));
	vec2 Hit;
	EXPECT_EQ(m_Collision.IntersectLineTeleHook(vec2(100, 150), vec2(250, 150), &Hit, nullptr), TILE_SOLID);
	EXPECT_NEAR(Hit.x, 150, 0.01f);
}

TEST_F(CAxiomCollisionTest, UnknownLayersAndParallaxRemainDecorative)
{
	AddQuad("decoration", Rect(100, 100, 100, 100));
	AddQuad("QHook", Rect(200, 100, 100, 100));
	m_QuadGroup.m_ParallaxX = 50;
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_FALSE(m_Collision.CheckPoint(150, 150));
	EXPECT_FALSE(m_Collision.CheckPoint(250, 150));
}

TEST_F(CAxiomCollisionTest, DegeneratePolygonAndInvalidEnvelopeAreSafe)
{
	AddQuad("QHook", Rect(100, 100, 0, 100));
	CQuad Quad = Rect(200, 100, 100, 100);
	Quad.m_PosEnv = 12;
	AddQuad("QHook", Quad);
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_FALSE(m_Collision.CheckPoint(100, 150));
	EXPECT_TRUE(m_Collision.CheckPoint(250, 150));
	m_Collision.SetAxiomQuadTime(std::numeric_limits<double>::quiet_NaN());
	EXPECT_TRUE(m_Collision.CheckPoint(250, 150));
}

TEST_F(CAxiomCollisionTest, AnimatedSolidPushesStationaryPlayer)
{
	CQuad Quad = Rect(100, 100, 30, 100);
	Quad.m_PosEnv = 0;
	AddQuad("QHook", Quad);
	CEnvPoint A{}, B{};
	A.m_Curvetype = CURVETYPE_LINEAR;
	B.m_Time = CFixedTime(1000);
	B.m_aValues[0] = f2fx(100);
	Envelope(3, {A, B});
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	vec2 Pos(175, 150), Vel(0, 0);
	EXPECT_FALSE(m_Collision.TestBox(Pos, vec2(28, 28)));
	m_Collision.SetAxiomQuadTick(25, 50);
	m_Collision.MoveBox(&Pos, &Vel, vec2(28, 28), vec2(0, 0));
	EXPECT_FALSE(m_Collision.TestBox(Pos, vec2(28, 28)));
	EXPECT_GT(distance(Pos, vec2(175, 150)), 1);
}

TEST_F(CAxiomCollisionTest, TriggerLayersAreNonSolid)
{
	AddQuad("QFr", Rect(100, 100, 100, 100));
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_EQ(m_Collision.GetCollisionAt(150, 150), TILE_FREEZE);
	EXPECT_TRUE(m_Collision.HasAxiomQuadTile(vec2(150, 150), TILE_FREEZE));
	EXPECT_FALSE(m_Collision.CheckPoint(150, 150));
	EXPECT_FALSE(m_Collision.TestBox(vec2(150, 150), vec2(28, 28)));
	EXPECT_EQ(m_Collision.IntersectLine(vec2(0, 150), vec2(250, 150), nullptr, nullptr), 0);
}

TEST_F(CAxiomCollisionTest, OverlappingTriggersKeepLayerOrder)
{
	AddQuad("QFr", Rect(100, 100, 100, 100));
	AddQuad("QUnFr", Rect(100, 100, 100, 100));
	AddQuad("QDeep", Rect(100, 100, 100, 100));
	AddQuad("QUnDeep", Rect(100, 100, 100, 100));
	AddQuad("QLiveFr", Rect(100, 100, 100, 100));
	AddQuad("QReJump", Rect(100, 100, 100, 100));
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_EQ(m_Collision.GetAxiomQuadTiles(vec2(150, 150)), (std::vector<int>{TILE_FREEZE, TILE_UNFREEZE, TILE_DFREEZE, TILE_DUNFREEZE, TILE_LFREEZE, TILE_REFILL_JUMPS}));
	EXPECT_FALSE(m_Collision.CheckPoint(150, 150));
}

TEST_F(CAxiomCollisionTest, DdnetBezierAnimationAndHook)
{
	CQuad Quad = Rect(100, 100, 100, 100);
	Quad.m_PosEnv = 0;
	AddQuad("QHook", Quad);
	BezierEnvelope(false);
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	int Anchor;
	vec2 Local, Hook;
	ASSERT_TRUE(m_Collision.FindAxiomHookAnchor(vec2(150, 150), &Anchor, &Local));
	m_Collision.SetAxiomQuadTime(0.25);
	ASSERT_TRUE(m_Collision.GetAxiomHookAnchor(Anchor, Local, &Hook));
	EXPECT_NEAR(Hook.x, 165.64f, 0.03f);
	EXPECT_NEAR(Hook.y, 150, 0.001f);
}

TEST_F(CAxiomCollisionTest, UpstreamBezierAnimation)
{
	CQuad Quad = Rect(100, 100, 100, 100);
	Quad.m_PosEnv = 0;
	AddQuad("QHook", Quad);
	BezierEnvelope(true);
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	m_Collision.SetAxiomQuadTime(0.25);
	EXPECT_FALSE(m_Collision.CheckPoint(110, 150));
	EXPECT_TRUE(m_Collision.CheckPoint(215, 150));
}

TEST_F(CAxiomCollisionTest, TuneQuadUsesAxiomColorOffset)
{
	CQuad Quad = Rect(100, 100, 100, 100);
	Quad.m_ColorEnvOffset = 5;
	AddQuad("QTune", Quad);
	Init();
	m_Collision.SetAxiomQuadCollision(true);
	EXPECT_EQ(m_Collision.GetAxiomQuadTuneZone(vec2(150, 150)), 5);
	EXPECT_EQ(m_Collision.GetAxiomQuadTuneZone(vec2(50, 150)), 0);
}
