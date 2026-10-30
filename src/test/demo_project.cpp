#include <engine/shared/demo_project.h>
#include <gtest/gtest.h>

using demo_edit::CProject;

TEST(DemoProject, SplitRangesAreDisjoint)
{
	CProject P;
	P.Reset("source.demo", 100, 1100, 50);
	ASSERT_TRUE(P.Add(100, 600));
	EXPECT_FALSE(P.Split(0, 100));
	EXPECT_FALSE(P.Split(0, 600));
	ASSERT_TRUE(P.Split(0, 350));
	ASSERT_EQ(P.Clips().size(), 2u);
	EXPECT_EQ(P.Clips()[0].m_Out, P.Clips()[1].m_In);
	EXPECT_EQ(P.Clips()[0].m_In, 100);
	EXPECT_EQ(P.Clips()[1].m_Out, 600);
	EXPECT_DOUBLE_EQ(P.Duration(), 10);
	EXPECT_EQ(P.Position(5).m_Clip, 1);
	EXPECT_EQ(P.Position(5).m_SourceTick, 350);
}

TEST(DemoProject, OrderingSpeedAndPosition)
{
	CProject P;
	P.Reset("source.demo", 0, 1000, 50);
	ASSERT_TRUE(P.Add(0, 100));
	ASSERT_TRUE(P.Add(500, 700));
	ASSERT_TRUE(P.SetSpeed(1, 200));
	EXPECT_DOUBLE_EQ(P.Duration(), 4);
	EXPECT_EQ(P.Position(3).m_SourceTick, 600);
	ASSERT_TRUE(P.Move(1, 0));
	EXPECT_EQ(P.Position(1).m_SourceTick, 600);
	EXPECT_EQ(P.Position(2).m_SourceTick, 0);
	EXPECT_EQ(P.Position(999).m_SourceTick, 99);
	EXPECT_EQ(P.Position(-5).m_SourceTick, 500);
	EXPECT_FALSE(P.SetSpeed(0, 0));
	EXPECT_FALSE(P.SetSpeed(0, 401));
}

TEST(DemoProject, UndoRedoAndNewBranch)
{
	CProject P;
	P.Reset("source.demo", 0, 1000, 50);
	ASSERT_TRUE(P.Add(0, 500));
	ASSERT_TRUE(P.Trim(0, 100, 400));
	ASSERT_TRUE(P.Duplicate(0));
	ASSERT_TRUE(P.Remove(0));
	ASSERT_TRUE(P.Undo());
	EXPECT_EQ(P.Clips().size(), 2u);
	ASSERT_TRUE(P.Undo());
	ASSERT_TRUE(P.Undo());
	EXPECT_EQ(P.Clips()[0].m_In, 0);
	ASSERT_TRUE(P.Redo());
	EXPECT_EQ(P.Clips()[0].m_In, 100);
	ASSERT_TRUE(P.Split(0, 200));
	EXPECT_FALSE(P.CanRedo());
}

TEST(DemoProject, SaveLoadAndInvalidInputIsAtomic)
{
	CProject P;
	P.Reset("C:/source.demo", 100, 1100, 50);
	ASSERT_TRUE(P.Add(120, 320));
	ASSERT_TRUE(P.Rename(0, "冻结救援"));
	ASSERT_TRUE(P.SetSpeed(0, 125));
	CProject Q;
	std::string Error;
	ASSERT_TRUE(Q.Load(P.Serialize(), Error));
	EXPECT_EQ(Q.Serialize(), P.Serialize());
	EXPECT_FALSE(Q.CanUndo());
	const std::string Before = Q.Serialize();
	for(const auto &Bad : {std::string("{}"), std::string("{\"clips\":[null]}"), std::string("[1]"), std::string(1024 * 1024 + 1, ' ')})
	{
		EXPECT_FALSE(Q.Load(Bad, Error));
		EXPECT_EQ(Q.Serialize(), Before);
	}
	std::string Bad = Before;
	const auto Index = Bad.find("125");
	ASSERT_NE(Index, std::string::npos);
	Bad.replace(Index, 3, "999999999999");
	EXPECT_FALSE(Q.Load(Bad, Error));
	EXPECT_EQ(Q.Serialize(), Before);
}

TEST(DemoProject, LimitsAndHistory)
{
	CProject P;
	P.Reset("source.demo", 0, 1000, 50);
	EXPECT_FALSE(P.Add(-1, 100));
	EXPECT_FALSE(P.Add(100, 100));
	EXPECT_FALSE(P.Add(100, 1001));
	for(int I = 0; I < CProject::MAX_CLIPS; ++I) ASSERT_TRUE(P.Add(I, I + 1));
	EXPECT_FALSE(P.Duplicate(0));
	EXPECT_FALSE(P.Add(0, 1));
	for(int I = 0; I < 120; ++I) ASSERT_TRUE(P.SetSpeed(0, 101 + I % 2));
	int Undos = 0;
	while(P.Undo()) ++Undos;
	EXPECT_EQ(Undos, 100);
}
