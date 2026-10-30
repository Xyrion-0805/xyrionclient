/* Quad collision rules adapted from TeeworldsAxiom/teeworlds-Axiom. */
#ifndef GAME_AXIOM_QUAD_COLLISION_H
#define GAME_AXIOM_QUAD_COLLISION_H

#include <base/vmath.h>
#include <game/mapitems.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

class CLayers;
class CEnvelopeTriggerRuntime;

// Owned by an explicitly enabled client CCollision. No global config or clock
// is read here: prediction can rewind and replay this object deterministically.
class CAxiomQuadCollision
{
public:
	void Init(CLayers *pLayers);
	void SetTime(double Seconds, const CEnvelopeTriggerRuntime *pTriggers = nullptr, int ClientId = -1, int Team = 0, double Tick = 0);
	int TileAt(vec2 Pos, bool SolidOnly = false) const;
	bool HasTile(vec2 Pos, int Tile) const;
	std::vector<int> TilesAt(vec2 Pos) const;
	int TuneZoneAt(vec2 Pos) const;
	bool TestBox(vec2 Pos, vec2 HalfSize) const;
	bool Pushout(vec2 Pos, vec2 HalfSize, vec2 *pPush, vec2 *pNormal) const;
	int IntersectLine(vec2 From, vec2 To, float *pFraction, vec2 *pNormal = nullptr) const;
	bool FindHookAnchor(vec2 Pos, int *pQuadIndex, vec2 *pLocalPos) const;
	bool HookAnchor(int QuadIndex, vec2 LocalPos, vec2 *pWorldPos) const;
	size_t NumQuads() const { return m_vQuads.size(); }

private:
	struct CEnvelope
	{
		int m_Channels = 0;
		std::vector<CEnvPoint> m_vPoints;
		std::vector<CEnvPointBezier> m_vBezier;
	};
	struct CCollisionQuad
	{
		CQuad m_Quad;
		int m_Tile;
		vec2 m_GroupOffset;
		bool m_Clipped;
		vec2 m_ClipMin;
		vec2 m_ClipMax;
		vec2 m_Position = vec2(0, 0);
		float m_Angle = 0;
		bool m_Active = false;
		vec2 m_Min = vec2(0, 0);
		vec2 m_Max = vec2(0, 0);
		std::vector<vec2> m_vPolygon;
		std::vector<vec2> m_vClipScratch;
		int m_PositionSample = -1;
		int m_ColorSample = -1;
		bool m_GeometryInitialized = false;
		bool m_Visible = false;
		bool m_Indexed = false;
		bool m_Large = false;
		int m_MinCellX = 0, m_MinCellY = 0;
		int m_MaxCellX = 0, m_MaxCellY = 0;
	};
	struct CEnvelopeSample
	{
		int m_Envelope;
		int m_Offset;
		int m_Channels;
		bool m_Animated;
		std::array<float, 4> m_aValues;
		double m_LastTime = -1e100;
	};
	void LoadEnvelopes(CLayers *pLayers);
	void EvalEnvelope(int Index, double Seconds, float *pValues, int Channels) const;
	void UpdateQuad(int Index);
	void UpdateQuadIndex(int Index);
	void QueryBox(vec2 Min, vec2 Max, std::vector<int> &vIndices) const;
	const std::vector<int> &QueryPoint(vec2 Pos, std::vector<int> &vFallback) const;
	static bool BoundsOverlap(const CCollisionQuad &Quad, vec2 Min, vec2 Max);
	static bool PointInside(vec2 Pos, const std::vector<vec2> &vPolygon);
	static bool BoxPushout(vec2 Pos, vec2 HalfSize, const std::vector<vec2> &vPolygon, vec2 *pPush, vec2 *pNormal);

	std::vector<CEnvelope> m_vEnvelopes;
	std::vector<CCollisionQuad> m_vQuads;
	std::vector<CEnvelopeSample> m_vEnvelopeSamples;
	std::vector<int> m_vAnimatedQuads;
	std::unordered_map<uint64_t, std::vector<int>> m_Grid;
	std::vector<int> m_vLargeQuads;
	double m_Time = -1;
	const CEnvelopeTriggerRuntime *m_pTriggers = nullptr;
	int m_ClientId = -1, m_Team = 0;
	double m_TriggerTick = -1;
};

#endif
