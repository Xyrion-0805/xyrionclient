#include <engine/client/dummy_session.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{
CSnapshotBuffer MakeSnapshot(int Value)
{
	CSnapshotBuilder Builder;
	Builder.Init();
	CNetObj_Flag Flag = {Value, 0, 0};
	Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag));
	CSnapshotBuffer Result;
	Builder.Finish(&Result);
	return Result;
}

bool Receive(CDummySession &Session, CSnapshotDelta &Delta, int Tick, int BaseTick, const CSnapshot *pBase, const CSnapshot *pCurrent, bool BadCrc = false)
{
	CSnapshotDeltaBuffer Diff;
	const int DeltaSize = Delta.CreateDelta(pBase, pCurrent, &Diff);
	std::array<unsigned char, CSnapshot::MAX_SIZE> Compressed;
	const int Size = CVariableInt::Compress(Diff.m_aData, DeltaSize, Compressed.data(), Compressed.size());
	CPacker Packer;
	Packer.Reset();
	Packer.AddInt(Tick);
	Packer.AddInt(Tick - BaseTick);
	Packer.AddInt(pCurrent->Crc() + (BadCrc ? 1 : 0));
	Packer.AddInt(Size);
	Packer.AddRaw(Compressed.data(), Size);
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	return Session.ReceiveSnapshot(NETMSG_SNAPSINGLE, Unpacker, Delta, Tick);
}
}

TEST(XyrionDummySession, ThreeIndependentDeltaStreams)
{
	auto pDelta = std::make_unique<CSnapshotDelta>();
	std::array<CDummySession, 3> Sessions;
	for(int i = 0; i < 3; i++)
	{
		const auto First = MakeSnapshot(100 + i);
		const auto Second = MakeSnapshot(200 + i);
		ASSERT_TRUE(Receive(Sessions[i], *pDelta, 10 + i, -1, CSnapshot::EmptySnapshot(), First.AsSnapshot()));
		ASSERT_TRUE(Receive(Sessions[i], *pDelta, 20 + i, 10 + i, First.AsSnapshot(), Second.AsSnapshot()));
		EXPECT_EQ(Sessions[i].m_AckTick, 20 + i);
	}
	Sessions[1].Reset();
	EXPECT_EQ(Sessions[0].m_AckTick, 20);
	EXPECT_EQ(Sessions[1].m_AckTick, -1);
	EXPECT_EQ(Sessions[2].m_AckTick, 22);
}

TEST(XyrionDummySession, MissingBaseAndBadCrcRequestFullSnapshot)
{
	auto pDelta = std::make_unique<CSnapshotDelta>();
	CDummySession Session;
	const auto First = MakeSnapshot(100);
	const auto Second = MakeSnapshot(200);
	ASSERT_TRUE(Receive(Session, *pDelta, 10, -1, CSnapshot::EmptySnapshot(), First.AsSnapshot()));
	EXPECT_FALSE(Receive(Session, *pDelta, 20, 15, First.AsSnapshot(), Second.AsSnapshot()));
	EXPECT_EQ(Session.m_AckTick, -1);
	EXPECT_FALSE(Receive(Session, *pDelta, 21, -1, CSnapshot::EmptySnapshot(), Second.AsSnapshot(), true));
	EXPECT_EQ(Session.m_AckTick, -1);
	EXPECT_TRUE(Receive(Session, *pDelta, 22, -1, CSnapshot::EmptySnapshot(), Second.AsSnapshot()));
	EXPECT_EQ(Session.m_AckTick, 22);
	EXPECT_FALSE(Receive(Session, *pDelta, 10, -1, CSnapshot::EmptySnapshot(), First.AsSnapshot()));
	EXPECT_EQ(Session.m_AckTick, 22);
}

TEST(XyrionDummySession, RejectsInvalidMultipartWithoutUpdatingAck)
{
	auto pDelta = std::make_unique<CSnapshotDelta>();
	CDummySession Session;
	for(const auto [Parts, Part, Size] : {std::array<int, 3>{65, 0, 0}, {2, 2, 0}, {2, 0, 1}, {1, 0, -1}, {64, 63, MAX_SNAPSHOT_PACKSIZE + 1}})
	{
		CPacker Packer;
		Packer.Reset();
		for(int Value : {100, 101, Parts, Part, 0, Size})
			Packer.AddInt(Value);
		CUnpacker Unpacker;
		Unpacker.Reset(Packer.Data(), Packer.Size());
		EXPECT_FALSE(Session.ReceiveSnapshot(NETMSG_SNAP, Unpacker, *pDelta, 100));
		EXPECT_EQ(Session.m_AckTick, -1);
	}
}

TEST(XyrionDummySession, ParkingReleasesActionsAndPreservesCounters)
{
	CDummySession Session;
	Session.m_Input.m_Direction = 1;
	Session.m_Input.m_Hook = Session.m_Input.m_Jump = 1;
	Session.m_Input.m_Fire = 63;
	Session.m_Input.m_NextWeapon = 3;
	Session.ReleaseInput();
	EXPECT_EQ(Session.m_Input.m_Direction, 0);
	EXPECT_EQ(Session.m_Input.m_Hook, 0);
	EXPECT_EQ(Session.m_Input.m_Jump, 0);
	EXPECT_EQ(Session.m_Input.m_Fire, 0);
	EXPECT_EQ(Session.m_Input.m_NextWeapon, 4);
	Session.ReleaseInput();
	EXPECT_EQ(Session.m_Input.m_Fire, 0);
	EXPECT_EQ(Session.m_Input.m_NextWeapon, 4);
}

TEST(XyrionDummySession, MultipartCanArriveInReverseOrder)
{
	auto pDelta = std::make_unique<CSnapshotDelta>();
	CDummySession Session;
	std::array<int, 1000> Values;
	for(size_t i = 0; i < Values.size(); i++)
		Values[i] = static_cast<int>(i * 104729);
	CSnapshotBuilder Builder;
	Builder.Init();
	ASSERT_TRUE(Builder.NewItem(50, 0, Values.data(), sizeof(Values)));
	CSnapshotBuffer Snap;
	Builder.Finish(&Snap);
	CSnapshotDeltaBuffer Diff;
	const int DeltaSize = pDelta->CreateDelta(CSnapshot::EmptySnapshot(), Snap.AsSnapshot(), &Diff);
	std::array<unsigned char, CSnapshot::MAX_SIZE> Compressed;
	const int Size = CVariableInt::Compress(Diff.m_aData, DeltaSize, Compressed.data(), Compressed.size());
	const int Parts = (Size + MAX_SNAPSHOT_PACKSIZE - 1) / MAX_SNAPSHOT_PACKSIZE;
	ASSERT_GT(Parts, 1);
	for(int Part = Parts - 1; Part >= 0; Part--)
	{
		CPacker Packer;
		Packer.Reset();
		const int PartSize = std::min<int>(MAX_SNAPSHOT_PACKSIZE, Size - Part * MAX_SNAPSHOT_PACKSIZE);
		for(int Value : {100, 101, Parts, Part, static_cast<int>(Snap.AsSnapshot()->Crc()), PartSize})
			Packer.AddInt(Value);
		Packer.AddRaw(Compressed.data() + Part * MAX_SNAPSHOT_PACKSIZE, PartSize);
		CUnpacker Unpacker;
		Unpacker.Reset(Packer.Data(), Packer.Size());
		EXPECT_EQ(Session.ReceiveSnapshot(NETMSG_SNAP, Unpacker, *pDelta, 100), Part == 0);
		EXPECT_EQ(Session.m_AckTick, Part == 0 ? 100 : -1);
	}
	ASSERT_NE(Session.Snapshot(), nullptr);
	EXPECT_EQ(Session.Snapshot()->Crc(), Snap.AsSnapshot()->Crc());
}

TEST(XyrionDummySession, CopyMovesUsesOwnFireCounterAndReleasesWhenDisabled)
{
	CDummySession Session;
	CNetObj_PlayerInput Source = {};
	Source.m_Fire = 42;
	Source.m_Direction = Source.m_Hook = 1;
	Session.CopyInput(&Source);
	EXPECT_EQ(Session.m_Input.m_Fire, 0);
	Source.m_Fire++;
	Session.CopyInput(&Source);
	EXPECT_EQ(Session.m_Input.m_Fire, 1);
	EXPECT_EQ(Session.m_Input.m_Direction, 1);
	Session.CopyInput(nullptr);
	EXPECT_EQ(Session.m_Input.m_Fire, 2);
	EXPECT_EQ(Session.m_Input.m_Direction, 0);
	EXPECT_EQ(Session.m_Input.m_Hook, 0);
}
