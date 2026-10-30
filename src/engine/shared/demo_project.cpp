#include "demo_project.h"

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace demo_edit
{
void CProject::Reset(std::string Source, int FirstTick, int LastTick, int TickSpeed)
{
	m_Source = std::move(Source);
	m_FirstTick = FirstTick;
	m_LastTick = LastTick;
	m_TickSpeed = std::max(1, TickSpeed);
	m_vClips.clear();
	m_vUndo.clear();
	m_vRedo.clear();
}

bool CProject::ValidRange(int In, int Out) const
{
	return In >= m_FirstTick && Out <= m_LastTick && In < Out;
}

void CProject::Checkpoint()
{
	if(m_vUndo.size() >= 100) m_vUndo.erase(m_vUndo.begin());
	m_vUndo.push_back(m_vClips);
	m_vRedo.clear();
}

bool CProject::Add(int In, int Out)
{
	if(!ValidRange(In, Out) || m_vClips.size() >= MAX_CLIPS) return false;
	Checkpoint();
	m_vClips.push_back({In, Out, 100, "片段 " + std::to_string(m_vClips.size() + 1)});
	return true;
}

bool CProject::Trim(int Index, int In, int Out)
{
	if(Index < 0 || Index >= (int)m_vClips.size() || !ValidRange(In, Out)) return false;
	if(m_vClips[Index].m_In == In && m_vClips[Index].m_Out == Out) return false;
	Checkpoint();
	m_vClips[Index].m_In = In;
	m_vClips[Index].m_Out = Out;
	return true;
}

bool CProject::Split(int Index, int SourceTick)
{
	if(Index < 0 || Index >= (int)m_vClips.size() || m_vClips.size() >= MAX_CLIPS) return false;
	const auto Clip = m_vClips[Index];
	if(SourceTick <= Clip.m_In || SourceTick >= Clip.m_Out) return false;
	Checkpoint();
	m_vClips[Index].m_Out = SourceTick;
	auto Right = Clip;
	Right.m_In = SourceTick;
	m_vClips.insert(m_vClips.begin() + Index + 1, std::move(Right));
	return true;
}

bool CProject::Remove(int Index)
{
	if(Index < 0 || Index >= (int)m_vClips.size()) return false;
	Checkpoint();
	m_vClips.erase(m_vClips.begin() + Index);
	return true;
}

bool CProject::Duplicate(int Index)
{
	if(Index < 0 || Index >= (int)m_vClips.size() || m_vClips.size() >= MAX_CLIPS) return false;
	Checkpoint();
	const auto Copy = m_vClips[Index];
	m_vClips.insert(m_vClips.begin() + Index + 1, Copy);
	return true;
}

bool CProject::Move(int From, int To)
{
	if(From < 0 || To < 0 || From >= (int)m_vClips.size() || To >= (int)m_vClips.size() || From == To) return false;
	Checkpoint();
	auto Clip = m_vClips[From];
	m_vClips.erase(m_vClips.begin() + From);
	m_vClips.insert(m_vClips.begin() + To, std::move(Clip));
	return true;
}

bool CProject::SetSpeed(int Index, int Percent)
{
	if(Index < 0 || Index >= (int)m_vClips.size() || Percent < 25 || Percent > 400 || m_vClips[Index].m_Speed == Percent) return false;
	Checkpoint();
	m_vClips[Index].m_Speed = Percent;
	return true;
}

bool CProject::Rename(int Index, std::string Name)
{
	if(Index < 0 || Index >= (int)m_vClips.size() || Name.empty() || Name.size() > 128 || m_vClips[Index].m_Name == Name) return false;
	Checkpoint();
	m_vClips[Index].m_Name = std::move(Name);
	return true;
}

bool CProject::Undo()
{
	if(m_vUndo.empty()) return false;
	m_vRedo.push_back(m_vClips);
	m_vClips = std::move(m_vUndo.back());
	m_vUndo.pop_back();
	return true;
}

bool CProject::Redo()
{
	if(m_vRedo.empty()) return false;
	m_vUndo.push_back(m_vClips);
	m_vClips = std::move(m_vRedo.back());
	m_vRedo.pop_back();
	return true;
}

double CProject::Duration(int Index) const
{
	if(Index < 0 || Index >= (int)m_vClips.size()) return 0;
	const auto &Clip = m_vClips[Index];
	return (Clip.m_Out - Clip.m_In) * 100.0 / (m_TickSpeed * Clip.m_Speed);
}

double CProject::Duration() const { return ClipStart(m_vClips.size()); }

double CProject::ClipStart(int Index) const
{
	double Result = 0;
	for(int I = 0; I < std::min(Index, (int)m_vClips.size()); ++I) Result += Duration(I);
	return Result;
}

SPosition CProject::Position(double Seconds) const
{
	if(m_vClips.empty() || !std::isfinite(Seconds)) return {};
	Seconds = std::max(0.0, Seconds);
	for(int I = 0; I < (int)m_vClips.size(); ++I)
	{
		if(Seconds < Duration(I) || I == (int)m_vClips.size() - 1)
		{
			const auto &Clip = m_vClips[I];
			const int Offset = (int)std::clamp(Seconds * m_TickSpeed * Clip.m_Speed / 100.0, 0.0, (double)(Clip.m_Out - Clip.m_In - 1));
			return {I, Clip.m_In + Offset};
		}
		Seconds -= Duration(I);
	}
	return {};
}

std::string CProject::Serialize() const
{
	CJsonStringWriter W;
	W.BeginObject();
	W.WriteAttribute("format"); W.WriteStrValue("xyrion-demo-project");
	W.WriteAttribute("version"); W.WriteIntValue(1);
	W.WriteAttribute("source"); W.WriteStrValue(m_Source.c_str());
	W.WriteAttribute("first_tick"); W.WriteIntValue(m_FirstTick);
	W.WriteAttribute("last_tick"); W.WriteIntValue(m_LastTick);
	W.WriteAttribute("tick_speed"); W.WriteIntValue(m_TickSpeed);
	W.WriteAttribute("clips"); W.BeginArray();
	for(const auto &Clip : m_vClips)
	{
		W.BeginObject();
		W.WriteAttribute("in"); W.WriteIntValue(Clip.m_In);
		W.WriteAttribute("out"); W.WriteIntValue(Clip.m_Out);
		W.WriteAttribute("speed"); W.WriteIntValue(Clip.m_Speed);
		W.WriteAttribute("name"); W.WriteStrValue(Clip.m_Name.c_str());
		W.EndObject();
	}
	W.EndArray(); W.EndObject();
	return W.GetOutputString();
}

bool CProject::Load(const std::string &Json, std::string &Error)
{
	Error = "工程格式或片段范围无效";
	if(Json.size() > 1024 * 1024) return false;
	std::unique_ptr<json_value, decltype(&json_value_free)> Root(JsonParse(Json.c_str(), Json.size()), json_value_free);
	if(!Root || Root->type != json_object) return false;
	const auto Get = [&](const char *pKey) { return json_object_get(Root.get(), pKey); };
	const auto Integer = [&](const char *pKey) { return Get(pKey)->type == json_integer && Get(pKey)->u.integer >= 0 && Get(pKey)->u.integer <= 1000000000; };
	if(Get("format")->type != json_string || std::string(json_string_get(Get("format"))) != "xyrion-demo-project" ||
		Get("version")->type != json_integer || json_int_get(Get("version")) != 1 ||
		Get("source")->type != json_string || Get("source")->u.string.length == 0 || Get("source")->u.string.length >= 1024 ||
		!Integer("first_tick") || !Integer("last_tick") || !Integer("tick_speed") ||
		json_int_get(Get("last_tick")) <= json_int_get(Get("first_tick")) || json_int_get(Get("tick_speed")) != 50 ||
		Get("clips")->type != json_array || json_array_length(Get("clips")) > MAX_CLIPS) return false;
	CProject Candidate;
	Candidate.Reset(json_string_get(Get("source")), json_int_get(Get("first_tick")), json_int_get(Get("last_tick")), json_int_get(Get("tick_speed")));
	for(int I = 0; I < json_array_length(Get("clips")); ++I)
	{
		const auto *pClip = json_array_get(Get("clips"), I);
		const auto *pIn = json_object_get(pClip, "in"), *pOut = json_object_get(pClip, "out"), *pSpeed = json_object_get(pClip, "speed"), *pName = json_object_get(pClip, "name");
		if(pClip->type != json_object || pIn->type != json_integer || pOut->type != json_integer || pSpeed->type != json_integer ||
			pIn->u.integer < Candidate.m_FirstTick || pOut->u.integer > Candidate.m_LastTick || pIn->u.integer >= pOut->u.integer ||
			pSpeed->u.integer < 25 || pSpeed->u.integer > 400 || pName->type != json_string || pName->u.string.length == 0 || pName->u.string.length > 128) return false;
		Candidate.m_vClips.push_back({json_int_get(pIn), json_int_get(pOut), json_int_get(pSpeed), json_string_get(pName)});
	}
	*this = std::move(Candidate);
	Error.clear();
	return true;
}
}
