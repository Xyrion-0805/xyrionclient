#ifndef ENGINE_SHARED_DEMO_PROJECT_H
#define ENGINE_SHARED_DEMO_PROJECT_H

#include <string>
#include <vector>

namespace demo_edit
{
// Source ranges are [in, out): a split tick belongs only to the right clip.
struct SClip
{
	int m_In = 0;
	int m_Out = 0;
	int m_Speed = 100;
	std::string m_Name;
	bool operator==(const SClip &) const = default;
};

struct SPosition
{
	int m_Clip = -1;
	int m_SourceTick = 0;
};

class CProject
{
	std::string m_Source;
	int m_FirstTick = 0, m_LastTick = 0, m_TickSpeed = 50;
	std::vector<SClip> m_vClips;
	std::vector<std::vector<SClip>> m_vUndo, m_vRedo;
	void Checkpoint();
	bool ValidRange(int In, int Out) const;

public:
	static constexpr int MAX_CLIPS = 64;
	void Reset(std::string Source, int FirstTick, int LastTick, int TickSpeed);
	const std::string &Source() const { return m_Source; }
	int FirstTick() const { return m_FirstTick; }
	int LastTick() const { return m_LastTick; }
	int TickSpeed() const { return m_TickSpeed; }
	const std::vector<SClip> &Clips() const { return m_vClips; }
	bool Add(int In, int Out);
	bool Trim(int Index, int In, int Out);
	bool Split(int Index, int SourceTick);
	bool Remove(int Index);
	bool Duplicate(int Index);
	bool Move(int From, int To);
	bool SetSpeed(int Index, int Percent);
	bool Rename(int Index, std::string Name);
	bool Undo();
	bool Redo();
	bool CanUndo() const { return !m_vUndo.empty(); }
	bool CanRedo() const { return !m_vRedo.empty(); }
	double Duration(int Index) const;
	double Duration() const;
	double ClipStart(int Index) const;
	SPosition Position(double Seconds) const;
	std::string Serialize() const;
	bool Load(const std::string &Json, std::string &Error);
};
}
#endif
