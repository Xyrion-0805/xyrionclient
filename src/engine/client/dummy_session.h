#ifndef ENGINE_CLIENT_DUMMY_SESSION_H
#define ENGINE_CLIENT_DUMMY_SESSION_H

#include <base/mem.h>
#include <engine/shared/compression.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/snapshot.h>
#include <generated/protocol.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

// Raw snapshots and input belong to a network connection, independently of
// which dummy currently occupies the game client's second prediction slot.
class CDummySession
{
	CSnapshotStorage m_Storage;
	char m_aIncoming[CSnapshot::MAX_SIZE] = {};
	uint64_t m_Parts = 0;
	int m_RecvTick = -1;
	int m_NumParts = 0;
	int m_DeltaTick = -1;
	unsigned m_Crc = 0;
	int m_IncomingSize = 0;
	int m_LastSnapshotTick = -1;
	bool m_CopyMoves = false;
	CNetObj_PlayerInput m_LastCopyInput = {};

public:
	int m_AckTick = -1;
	int m_LocalId7 = -1;
	bool m_DidPostConnect = false;
	int64_t m_LastInputTime = 0;
	CNetObj_PlayerInput m_Input = {};
	std::vector<unsigned char> m_vTuning;

	CDummySession() { m_Input.m_TargetY = -1; }
	CDummySession(const CDummySession &) = delete;
	CDummySession &operator=(const CDummySession &) = delete;

	void Reset()
	{
		m_Storage.PurgeAll();
		m_Parts = 0;
		m_RecvTick = m_LastSnapshotTick = m_AckTick = -1;
		m_NumParts = m_IncomingSize = 0;
		m_LocalId7 = -1;
		m_DidPostConnect = false;
		m_LastInputTime = 0;
		m_Input = {};
		m_Input.m_TargetY = -1;
		m_vTuning.clear();
		m_CopyMoves = false;
	}

	void ReleaseInput()
	{
		m_Input.m_Direction = m_Input.m_Jump = m_Input.m_Hook = 0;
		m_Input.m_Fire = (m_Input.m_Fire + (m_Input.m_Fire & 1)) & INPUT_STATE_MASK;
		m_Input.m_NextWeapon = (m_Input.m_NextWeapon + (m_Input.m_NextWeapon & 1)) & INPUT_STATE_MASK;
		m_Input.m_PrevWeapon = (m_Input.m_PrevWeapon + (m_Input.m_PrevWeapon & 1)) & INPUT_STATE_MASK;
	}

	int LastSnapshotTick() const { return m_LastSnapshotTick; }
	const CSnapshot *Snapshot() const { return m_Storage.m_pLast ? m_Storage.m_pLast->m_pSnap : nullptr; }

	void CopyInput(const CNetObj_PlayerInput *pSource)
	{
		if(!pSource)
		{
			if(m_CopyMoves)
				ReleaseInput();
			m_CopyMoves = false;
			return;
		}
		if(m_CopyMoves)
		{
			m_Input.m_Fire = (m_Input.m_Fire + pSource->m_Fire - m_LastCopyInput.m_Fire) & INPUT_STATE_MASK;
			m_Input.m_NextWeapon = (m_Input.m_NextWeapon + pSource->m_NextWeapon - m_LastCopyInput.m_NextWeapon) & INPUT_STATE_MASK;
			m_Input.m_PrevWeapon = (m_Input.m_PrevWeapon + pSource->m_PrevWeapon - m_LastCopyInput.m_PrevWeapon) & INPUT_STATE_MASK;
		}
		m_Input.m_Direction = pSource->m_Direction;
		m_Input.m_Jump = pSource->m_Jump;
		m_Input.m_Hook = pSource->m_Hook;
		m_Input.m_TargetX = pSource->m_TargetX;
		m_Input.m_TargetY = pSource->m_TargetY;
		m_Input.m_WantedWeapon = pSource->m_WantedWeapon;
		m_Input.m_PlayerFlags = pSource->m_PlayerFlags;
		m_LastCopyInput = *pSource;
		m_CopyMoves = true;
	}

	bool ReceiveSnapshot(int Msg, CUnpacker &Unpacker, CSnapshotDelta &Delta, int64_t Now)
	{
		const int Tick = Unpacker.GetInt();
		const int64_t BaseTick = static_cast<int64_t>(Tick) - Unpacker.GetInt();
		const int NumParts = Msg == NETMSG_SNAP ? Unpacker.GetInt() : 1;
		const int Part = Msg == NETMSG_SNAP ? Unpacker.GetInt() : 0;
		const unsigned Crc = Msg == NETMSG_SNAPEMPTY ? 0 : static_cast<unsigned>(Unpacker.GetInt());
		const int Size = Msg == NETMSG_SNAPEMPTY ? 0 : Unpacker.GetInt();
		if(Unpacker.Error() || Tick < 0 || BaseTick < -1 || BaseTick >= Tick ||
			NumParts < 1 || NumParts > CSnapshot::MAX_PARTS || Part < 0 || Part >= NumParts ||
			Size < 0 || Size > MAX_SNAPSHOT_PACKSIZE ||
			Part * MAX_SNAPSHOT_PACKSIZE + Size > static_cast<int>(sizeof(m_aIncoming)) ||
			(Part < NumParts - 1 && Size != MAX_SNAPSHOT_PACKSIZE))
			return false;
		const void *pData = Unpacker.GetRaw(Size);
		if(Unpacker.Error() || Tick < m_RecvTick || Tick <= m_LastSnapshotTick)
			return false;
		if(Tick != m_RecvTick)
		{
			m_RecvTick = Tick;
			m_NumParts = NumParts;
			m_DeltaTick = static_cast<int>(BaseTick);
			m_Crc = Crc;
			m_Parts = 0;
			m_IncomingSize = 0;
		}
		if(NumParts != m_NumParts || BaseTick != m_DeltaTick || Crc != m_Crc)
			return false;
		mem_copy(m_aIncoming + Part * MAX_SNAPSHOT_PACKSIZE, pData, Size);
		m_Parts |= uint64_t{1} << Part;
		if(Part == NumParts - 1)
			m_IncomingSize = Part * MAX_SNAPSHOT_PACKSIZE + Size;
		const uint64_t Complete = NumParts == 64 ? std::numeric_limits<uint64_t>::max() : (uint64_t{1} << NumParts) - 1;
		if(m_Parts != Complete)
			return false;
		m_Parts = 0;

		const CSnapshot *pBase = CSnapshot::EmptySnapshot();
		if(m_DeltaTick >= 0 && m_Storage.Get(m_DeltaTick, nullptr, &pBase, nullptr) < 0)
		{
			m_AckTick = -1;
			return false;
		}
		CSnapshotDeltaBuffer DeltaBuffer;
		const void *pDelta = Delta.EmptyDelta();
		int DeltaSize = sizeof(int) * 3;
		if(m_IncomingSize)
		{
			DeltaSize = CVariableInt::Decompress(m_aIncoming, m_IncomingSize, DeltaBuffer.m_aData, sizeof(DeltaBuffer.m_aData));
			if(DeltaSize < 0)
				return false;
			pDelta = DeltaBuffer.m_aData;
		}
		CSnapshotBuffer Buffer;
		const int SnapSize = Delta.UnpackDelta(pBase, &Buffer, pDelta, DeltaSize);
		if(SnapSize < 0 || !Buffer.AsSnapshot()->IsValid(SnapSize) ||
			(Msg != NETMSG_SNAPEMPTY && Buffer.AsSnapshot()->Crc() != m_Crc))
		{
			m_AckTick = -1;
			return false;
		}
		// Keep a bounded history even when the server repeatedly sends full snapshots.
		m_Storage.PurgeUntil(std::max(m_DeltaTick, Tick - 150));
		m_Storage.Add(Tick, Now, SnapSize, Buffer.AsSnapshot(), 0, nullptr);
		m_LastSnapshotTick = m_AckTick = Tick;
		return true;
	}
};

#endif
