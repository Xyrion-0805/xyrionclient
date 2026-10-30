#include <engine/map.h>
#include <base/time.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/teamscore.h>
#include <game/tee_balance_physics.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
class CBalanceTestMap : public IMap
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

class CTeeBalanceTest : public ::testing::Test
{
protected:
	CBalanceTestMap m_Map;
	CLayers m_Layers;
	CCollision m_Collision;
	CTeeBalanceTest()
	{
		CMapItemGroup Group{};
		Group.m_Version = 3;
		Group.m_ParallaxX = Group.m_ParallaxY = 100;
		Group.m_NumLayers = 1;
		m_Map.AddItem(MAPITEMTYPE_GROUP, 0, Group);
		std::vector<CTile> Tiles(64 * 32);
		for(int y = 20; y < 32; ++y)
			for(int x = 0; x < 64; ++x) Tiles[y * 64 + x].m_Index = TILE_SOLID;
		CMapItemLayerTilemap Layer{};
		Layer.m_Layer.m_Type = LAYERTYPE_TILES;
		Layer.m_Version = 3;
		Layer.m_Width = 64;
		Layer.m_Height = 32;
		Layer.m_Flags = TILESLAYERFLAG_GAME;
		Layer.m_Data = m_Map.AddData(Tiles.size() * sizeof(CTile), Tiles.data());
		m_Map.AddItem(MAPITEMTYPE_LAYER, 0, Layer);
		m_Layers.Init(&m_Map, false, false);
		m_Collision.Init(&m_Layers);
	}
};
}


TEST_F(CTeeBalanceTest, EstablishAndHoldWithIdleUpper)
{
	for(int Direction : {-1, 1})
	for(bool Reverse : {false, true})
	for(bool NoWeak : {false, true})
	for(int Spacing : {40, 56, 80})
	{
		SCOPED_TRACE(::testing::Message() << "direction=" << Direction << " reverse=" << Reverse << " noweak=" << NoWeak << " spacing=" << Spacing);
		CWorldCore World;
		CTeamsCore Teams;
		CCharacterCore Cores[2]{};
		for(int i = 0; i < 2; ++i)
		{
			Cores[i].Init(&World, &m_Collision, &Teams);
			Cores[i].Reset();
			Cores[i].m_Id = i;
			World.m_apCharacters[i] = &Cores[i];
		}
		Cores[0].m_Pos = vec2(1024, 625);
		Cores[1].m_Pos = Cores[0].m_Pos + vec2(Direction * Spacing, 0);
		TeeBalance::CSimulation Initial(&m_Collision, Cores[0], Cores[1], Teams, {}, Reverse, NoWeak);
		TeeBalance::CPlanner Planner(Initial, Direction);
		while(!Planner.Finished()) Planner.Step();
		ASSERT_TRUE(Planner.Found());
		EXPECT_EQ(Cores[0].m_Pos, vec2(1024, 625));
		EXPECT_EQ(Cores[0].m_Vel, vec2(0, 0));
		for(auto Action : Planner.Result())
		{
			EXPECT_TRUE(Action.m_Direction == 0 || Action.m_Direction == Direction);
			Initial.Tick(Action);
			EXPECT_EQ(Initial.Upper().m_Input.m_Direction, 0);
			EXPECT_EQ(Initial.Upper().m_Input.m_Hook, 0);
			EXPECT_EQ(Initial.Upper().m_Input.m_Jump, 0);
		}
		for(int Tick = 0; Tick < 500; ++Tick)
		{
			Initial.Tick({Direction, false, false});
			EXPECT_EQ(Initial.Lower().m_Input.m_Direction, Direction);
			EXPECT_EQ(Initial.Lower().m_Input.m_Hook, 0);
			EXPECT_EQ(Initial.Upper().m_Input.m_Direction, 0);
			if(Tick >= 50) EXPECT_TRUE(TeeBalance::InFormation(Initial.Lower(), Initial.Upper(), Direction));
		}
	}
}

TEST_F(CTeeBalanceTest, WalkToStandWithMapFriction)
{
	for(int Direction : {-1, 1})
	for(float Friction : {0.25f, 0.5f, 0.75f, 0.9f})
	for(int Travel : {8, 17, 64, 320})
	{
		SCOPED_TRACE(::testing::Message() << "direction=" << Direction << " friction=" << Friction << " travel=" << Travel);
		CWorldCore World;
		CTeamsCore Teams;
		CCharacterCore Cores[2]{};
		for(int Id = 0; Id < 2; ++Id)
		{
			Cores[Id].Init(&World, &m_Collision, &Teams);
			Cores[Id].Reset();
			Cores[Id].m_Id = Id;
			World.m_apCharacters[Id] = &Cores[Id];
		}
		Cores[0].m_Pos = vec2(1024 - Direction * Travel, 625);
		Cores[0].m_Tuning.m_GroundFriction = Friction;
		Cores[1].m_Pos = vec2(1900, 625);
		TeeBalance::CSimulation Simulation(&m_Collision, Cores[0], Cores[1], Teams, {}, false, false);
		for(int Tick = 0; Tick < 256; ++Tick)
		{
			const auto &Lower = Simulation.Lower();
			const int Input = TeeBalance::WalkingDirection(Lower, 1024);
			if(Lower.m_Vel.x * Input < -0.1f) ADD_FAILURE() << "Used opposite-direction braking";
			Simulation.Tick({Input, false, false});
		}
		EXPECT_LE(std::abs(Simulation.Lower().m_Pos.x - 1024), 8);
		EXPECT_LT(std::abs(Simulation.Lower().m_Vel.x), 0.1f);
	}
}

TEST_F(CTeeBalanceTest, MirroredSearchAcrossStartingDistances)
{
	for(bool Reverse : {false, true})
	for(bool NoWeak : {false, true})
	for(int Spacing : {36, 43, 48, 63, 72, 96, 112, 128})
	{
		bool Found[2]{};
		for(int Side = 0; Side < 2; ++Side)
		{
			const int Direction = Side == 0 ? -1 : 1;
			SCOPED_TRACE(::testing::Message() << "direction=" << Direction << " reverse=" << Reverse << " noweak=" << NoWeak << " spacing=" << Spacing);
			CWorldCore World;
			CTeamsCore Teams;
			CCharacterCore Cores[2]{};
			for(int Id = 0; Id < 2; ++Id)
			{
				Cores[Id].Init(&World, &m_Collision, &Teams);
				Cores[Id].Reset();
				Cores[Id].m_Id = Id;
				World.m_apCharacters[Id] = &Cores[Id];
			}
			Cores[0].m_Pos = vec2(1024, 625);
			Cores[1].m_Pos = Cores[0].m_Pos + vec2(Direction * Spacing, 0);
			TeeBalance::CSimulation Simulation(&m_Collision, Cores[0], Cores[1], Teams, {}, Reverse, NoWeak);
			TeeBalance::CPlanner Planner(Simulation, Direction);
			while(!Planner.Finished()) Planner.Step(4);
			Found[Side] = Planner.Found();
			if(!Found[Side]) continue;
			for(auto Action : Planner.Result()) Simulation.Tick(Action);
			for(int Tick = 0; Tick < 250; ++Tick)
			{
				Simulation.Tick({Direction, false, false});
				if(Tick >= 50) ASSERT_TRUE(TeeBalance::InFormation(Simulation.Lower(), Simulation.Upper(), Direction));
			}
		}
		EXPECT_EQ(Found[0], Found[1]) << "Mirrored search differs at spacing " << Spacing;
		EXPECT_TRUE(Found[0]) << "No solution at spacing " << Spacing;
	}
}

TEST_F(CTeeBalanceTest, FailedSearchYieldsBetweenTicks)
{
	CWorldCore World;
	CTeamsCore Teams;
	CCharacterCore Cores[2]{};
	for(int Id = 0; Id < 2; ++Id)
	{
		Cores[Id].Init(&World, &m_Collision, &Teams);
		Cores[Id].Reset();
		Cores[Id].m_Id = Id;
		Cores[Id].m_Tuning.m_HammerStrength = 0;
		World.m_apCharacters[Id] = &Cores[Id];
		Cores[Id].m_Pos = vec2(1024 + Id * 48, 625);
	}
	TeeBalance::CSimulation Simulation(&m_Collision, Cores[0], Cores[1], Teams, {}, false, false);
	TeeBalance::CPlanner Planner(Simulation, 1);
	int Calls = 0;
	int64_t MaxSlice = 0;
	while(!Planner.Finished())
	{
		const int64_t Begin = time_get();
		Planner.Step(4);
		MaxSlice = std::max(MaxSlice, time_get() - Begin);
		++Calls;
	}
	EXPECT_FALSE(Planner.Found());
	EXPECT_GT(Calls, 1000);
	RecordProperty("max_slice_us", (int)(MaxSlice * 1000000 / time_freq()));
	std::printf("Failed search: %d resumable calls; maximum slice %lld us\n", Calls, (long long)(MaxSlice * 1000000 / time_freq()));
}

TEST_F(CTeeBalanceTest, RoomWallsDoNotInvalidateLocalBalance)
{
	auto *pTiles = reinterpret_cast<CTile *>(m_Map.m_vData[0].data());
	for(int Y = 0; Y < 20; ++Y)
		for(int X : {24, 40}) pTiles[Y * 64 + X].m_Index = TILE_SOLID;
	for(int X = 24; X <= 40; ++X) pTiles[16 * 64 + X].m_Index = TILE_SOLID;
	for(int Direction : {-1, 1})
	for(bool Reverse : {false, true})
	for(bool NoWeak : {false, true})
	{
		SCOPED_TRACE(::testing::Message() << "direction=" << Direction << " reverse=" << Reverse << " noweak=" << NoWeak);
		CWorldCore World;
		CTeamsCore Teams;
		CCharacterCore Cores[2]{};
		for(int Id = 0; Id < 2; ++Id)
		{
			Cores[Id].Init(&World, &m_Collision, &Teams);
			Cores[Id].Reset();
			Cores[Id].m_Id = Id;
			World.m_apCharacters[Id] = &Cores[Id];
			Cores[Id].m_Pos = vec2(1024 + Direction * Id * 56, 625);
		}
		TeeBalance::CSimulation Simulation(&m_Collision, Cores[0], Cores[1], Teams, {}, Reverse, NoWeak);
		TeeBalance::CPlanner Planner(Simulation, Direction);
		while(!Planner.Finished()) Planner.Step(4);
		ASSERT_TRUE(Planner.Found());
		for(auto Action : Planner.Result()) Simulation.Tick(Action);
		// Check the usable local balance before the pair reaches the side wall.
		for(int Tick = 0; Tick < 30; ++Tick) Simulation.Tick({Direction, false, false});
		EXPECT_TRUE(TeeBalance::InFormation(Simulation.Lower(), Simulation.Upper(), Direction))
			<< "actions=" << Planner.Result().size() << " lower=" << Simulation.Lower().m_Pos.x << ',' << Simulation.Lower().m_Pos.y
			<< " upper=" << Simulation.Upper().m_Pos.x << ',' << Simulation.Upper().m_Pos.y
			<< " upper_velocity=" << Simulation.Upper().m_Vel.x << ',' << Simulation.Upper().m_Vel.y;
		EXPECT_GT(Simulation.Lower().m_Pos.x, 24 * 32 + 28);
		EXPECT_LT(Simulation.Lower().m_Pos.x, 40 * 32 - 28);
	}
}

TEST_F(CTeeBalanceTest, CeilingInsideBalanceSpaceStillRejected)
{
	auto *pTiles = reinterpret_cast<CTile *>(m_Map.m_vData[0].data());
	for(int X = 0; X < 64; ++X) pTiles[18 * 64 + X].m_Index = TILE_SOLID;
	CWorldCore World;
	CTeamsCore Teams;
	CCharacterCore Cores[2]{};
	for(int Id = 0; Id < 2; ++Id)
	{
		Cores[Id].Init(&World, &m_Collision, &Teams);
		Cores[Id].Reset();
		Cores[Id].m_Id = Id;
		World.m_apCharacters[Id] = &Cores[Id];
		Cores[Id].m_Pos = vec2(1024 + Id * 56, 625);
	}
	TeeBalance::CSimulation Simulation(&m_Collision, Cores[0], Cores[1], Teams, {}, false, false);
	TeeBalance::CPlanner Planner(Simulation, 1);
	while(!Planner.Finished()) Planner.Step(4);
	EXPECT_FALSE(Planner.Found());
}

