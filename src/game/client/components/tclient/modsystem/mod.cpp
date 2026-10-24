#include "mod.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <functional>
#include <set>
#include <sstream>

namespace modsystem
{
TId NewId() { static std::atomic<TId> Next{1}; return Next.fetch_add(1, std::memory_order_relaxed); }
CRule *FindRule(CMod &Mod, TId Id) { for(auto &R : Mod.m_vRules) if(R.m_Id == Id) return &R; return nullptr; }
CScriptNode *FindNode(std::vector<CScriptNode> &Nodes, TId Id)
{
	for(auto &N : Nodes) { if(N.m_Id == Id) return &N; if(auto *p = FindNode(N.m_vThen, Id)) return p; if(auto *p = FindNode(N.m_vElse, Id)) return p; } return nullptr;
}
std::vector<CScriptNode> *ResolveSequence(CMod &Mod, const SSequenceTarget &Target)
{
	auto *pRule = FindRule(Mod, Target.m_Rule); if(!pRule) return nullptr;
	if(!Target.m_Parent) return &pRule->m_vBody;
	auto *pNode = FindNode(pRule->m_vBody, Target.m_Parent); if(!pNode || !pNode->HasChildren()) return nullptr;
	if(Target.m_Else) return pNode->m_Kind == CScriptNode::K_IF_ELSE && pNode->m_HasElse ? &pNode->m_vElse : nullptr;
	return &pNode->m_vThen;
}
namespace
{
void FreshIds(CScriptNode &Node)
{
	Node.m_Id = NewId();
	for(auto &Condition : Node.m_vIfConds) Condition.m_Id = NewId();
	for(auto &Child : Node.m_vThen) FreshIds(Child);
	for(auto &Child : Node.m_vElse) FreshIds(Child);
}
bool ValidUtf8(std::string_view Text)
{
	for(size_t i = 0; i < Text.size();)
	{
		unsigned char Lead = static_cast<unsigned char>(Text[i++]);
		if(Lead == 0) return false;
		if(Lead < 0x80) continue;
		int Extra;
		uint32_t Code, Minimum;
		if(Lead >= 0xc2 && Lead <= 0xdf) { Extra = 1; Code = Lead & 0x1f; Minimum = 0x80; }
		else if(Lead >= 0xe0 && Lead <= 0xef) { Extra = 2; Code = Lead & 0x0f; Minimum = 0x800; }
		else if(Lead >= 0xf0 && Lead <= 0xf4) { Extra = 3; Code = Lead & 0x07; Minimum = 0x10000; }
		else return false;
		if(i + Extra > Text.size()) return false;
		while(Extra--)
		{
			unsigned char Next = static_cast<unsigned char>(Text[i++]);
			if((Next & 0xc0) != 0x80) return false;
			Code = (Code << 6) | (Next & 0x3f);
		}
		if(Code < Minimum || Code > 0x10ffff || (Code >= 0xd800 && Code <= 0xdfff)) return false;
	}
	return true;
}
bool Erase(std::vector<CScriptNode> &Nodes, TId Id)
{
	for(auto It = Nodes.begin(); It != Nodes.end(); ++It) { if(It->m_Id == Id) { Nodes.erase(It); return true; } if(Erase(It->m_vThen, Id) || Erase(It->m_vElse, Id)) return true; } return false;
}
bool Locate(CMod &Mod, TId Id, SSequenceTarget &Target, size_t &Index)
{
	std::function<bool(std::vector<CScriptNode> &, SSequenceTarget)> Search = [&](std::vector<CScriptNode> &List, SSequenceTarget Seq) {
		for(size_t i = 0; i < List.size(); ++i) { auto &N = List[i]; if(N.m_Id == Id) { Target = Seq; Index = i; return true; } if(Search(N.m_vThen, {Seq.m_Rule, N.m_Id, false}) || Search(N.m_vElse, {Seq.m_Rule, N.m_Id, true})) return true; } return false;
	};
	for(auto &R : Mod.m_vRules) if(Search(R.m_vBody, {R.m_Id, 0, false})) return true;
	return false;
}
bool Contains(const CScriptNode &Node, TId Id)
{
	if(Node.m_Id == Id) return true;
	for(const auto &N : Node.m_vThen) if(Contains(N, Id)) return true;
	for(const auto &N : Node.m_vElse) if(Contains(N, Id)) return true;
	return false;
}
std::string Quote(std::string_view Text)
{
	std::string Out = "\"";
	for(char C : Text) { switch(C) { case '\\': Out += "\\\\"; break; case '"': Out += "\\\""; break; case '\n': Out += "\\n"; break; case '\r': Out += "\\r"; break; case '\t': Out += "\\t"; break; default: Out += C; } }
	return Out + '"';
}
bool Tokens(std::string_view Line, std::vector<std::string> &Out)
{
	size_t i = 0;
	while(i < Line.size())
	{
		while(i < Line.size() && std::isspace(static_cast<unsigned char>(Line[i]))) ++i;
		if(i == Line.size()) break;
		std::string Token;
		if(Line[i] == '"')
		{
			++i; bool Closed = false;
			while(i < Line.size()) { char C = Line[i++]; if(C == '"') { Closed = true; break; } if(C == '\\') { if(i == Line.size()) return false; C = Line[i++]; switch(C) { case 'n': C = '\n'; break; case 'r': C = '\r'; break; case 't': C = '\t'; break; case '\\': case '"': break; default: return false; } } Token += C; }
			if(!Closed || (i < Line.size() && !std::isspace(static_cast<unsigned char>(Line[i])))) return false;
		}
		else while(i < Line.size() && !std::isspace(static_cast<unsigned char>(Line[i]))) Token += Line[i++];
		Out.push_back(std::move(Token));
	}
	return true;
}
int Int(std::string_view Text, int Default = -1)
{
	int Value; auto R = std::from_chars(Text.data(), Text.data() + Text.size(), Value); return R.ec == std::errc() && R.ptr == Text.data() + Text.size() ? Value : Default;
}
bool ReadCondition(const std::vector<std::string> &T, size_t Offset, bool Modern, CCondition &Cond)
{
	if(Offset >= T.size()) return false;
	int Type = KeyIndex(CONDITION_KEYS, T[Offset++]); if(Type < 0) return false; Cond.m_Type = static_cast<ECondition>(Type);
	if(Modern) { if(T.size() != Offset + 4 || (T[Offset + 2] != "AND" && T[Offset + 2] != "OR") || (T[Offset + 3] != "0" && T[Offset + 3] != "1")) return false; Cond.m_Left = T[Offset]; Cond.m_Right = T[Offset + 1]; Cond.m_Logic = T[Offset + 2] == "OR" ? LOGIC_OR : LOGIC_AND; Cond.m_Not = T[Offset + 3] == "1"; }
	else if(ConditionUsesVarAndValue(Cond.m_Type))
	{
		if(Offset < T.size()) { auto Colon = T[Offset].find(':'); Cond.m_Left = T[Offset].substr(0, Colon); if(Colon != std::string::npos) Cond.m_Right = T[Offset].substr(Colon + 1); else if(Offset + 1 < T.size()) Cond.m_Right = T[Offset + 1]; }
	}
	else if(Cond.m_Type == COND_RANDOM_PROBABILITY && Offset < T.size()) Cond.m_Right = T[Offset];
	// Older editor builds wrote the displayed probability into the left field.
	// Prefer that visible value and normalize to the runtime/file value field.
	if(Cond.m_Type == COND_RANDOM_PROBABILITY && !Cond.m_Left.empty())
	{
		Cond.m_Right = std::move(Cond.m_Left);
		Cond.m_Left.clear();
	}
	return true;
}
bool ReadLegacyConditions(const std::vector<std::string> &Tokens, size_t Begin, std::vector<CCondition> &Conditions)
{
	while(Begin < Tokens.size())
	{
		const int Type = KeyIndex(CONDITION_KEYS, Tokens[Begin]);
		if(Type < 0 || Conditions.size() >= 64)
			return false;
		size_t End = Begin + 1;
		if(ConditionUsesVarAndValue(static_cast<ECondition>(Type)))
		{
			if(End == Tokens.size() || KeyIndex(CONDITION_KEYS, Tokens[End]) >= 0)
				return false;
			const bool PackedValue = Tokens[End].find(':') != std::string::npos;
			++End;
			if(!PackedValue && End < Tokens.size() && KeyIndex(CONDITION_KEYS, Tokens[End]) < 0)
				++End;
		}
		else if(Type == COND_RANDOM_PROBABILITY)
		{
			if(End == Tokens.size() || KeyIndex(CONDITION_KEYS, Tokens[End]) >= 0)
				return false;
			++End;
		}
		std::vector<std::string> Row(Tokens.begin() + static_cast<ptrdiff_t>(Begin), Tokens.begin() + static_cast<ptrdiff_t>(End));
		CCondition Cond;
		if(!ReadCondition(Row, 0, false, Cond))
			return false;
		Conditions.push_back(std::move(Cond));
		Begin = End;
	}
	return true;
}
void WriteConditions(std::ostringstream &Out, const std::vector<CCondition> &List, int Depth)
{
	for(const auto &C : List) Out << std::string(Depth, '\t') << "cond " << CONDITION_KEYS[C.m_Type] << ' ' << Quote(C.m_Left) << ' ' << Quote(C.m_Right) << ' ' << (C.m_Logic == LOGIC_OR ? "OR" : "AND") << ' ' << C.m_Not << '\n';
}
void WriteNodes(std::ostringstream &Out, const std::vector<CScriptNode> &List, int Depth)
{
	for(const auto &N : List)
	{
		std::string Indent(Depth, '\t');
		switch(N.m_Kind)
		{
		case CScriptNode::K_ACTION: Out << Indent << "act " << ACTION_KEYS[N.m_Block.m_Action] << ' ' << Quote(N.m_Block.m_Argument) << ' ' << N.m_Block.m_Target << ' ' << N.m_Block.m_Value << '\n'; break;
		case CScriptNode::K_WAIT: Out << Indent << "wait " << Quote(N.m_Count) << '\n'; break;
		case CScriptNode::K_LOOP: Out << Indent << "repeat " << Quote(N.m_Count) << '\n'; WriteNodes(Out, N.m_vThen, Depth + 1); Out << Indent << "endrepeat\n"; break;
		case CScriptNode::K_IF_ELSE:
			Out << Indent << "if\n"; WriteConditions(Out, N.m_vIfConds, Depth + 1); WriteNodes(Out, N.m_vThen, Depth + 1);
			if(N.m_HasElse || !N.m_vElse.empty()) { Out << Indent << "else\n"; WriteNodes(Out, N.m_vElse, Depth + 1); }
			Out << Indent << "endif\n"; break;
		}
	}
}
}
bool ValidateMod(const CMod &Mod, std::string &Error)
{
	Error.clear();
	if(Mod.m_Name.empty() || Mod.m_Name.size() > 128 || !ValidUtf8(Mod.m_Name)) { Error = "模组名称为空、过长或 UTF-8 无效"; return false; }
	if(Mod.m_vRules.size() > MAX_RULES || Mod.m_vVars.size() > 256 || Mod.m_vParams.size() > 256) { Error = "规则、变量或参数过多"; return false; }
	std::set<TId> Ids; Ids.insert(Mod.m_Id); int Count = 0;
	auto CheckId = [&](TId Id) { return Id != 0 && Ids.insert(Id).second; };
	auto CheckConditions = [&](const std::vector<CCondition> &List) { if(List.size() > 64) return false; for(const auto &C : List) if(!CheckId(C.m_Id) || C.m_Type < 0 || C.m_Type >= NUM_CONDITIONS || (C.m_Logic != LOGIC_AND && C.m_Logic != LOGIC_OR) || C.m_Left.size() > 4096 || C.m_Right.size() > 4096 || !ValidUtf8(C.m_Left) || !ValidUtf8(C.m_Right)) return false; return true; };
	std::function<bool(const std::vector<CScriptNode> &, int)> Check = [&](const std::vector<CScriptNode> &List, int Depth) {
		if(!List.empty() && Depth > MAX_DEPTH) { Error = "积木嵌套深度超过 8"; return false; }
		for(const auto &N : List)
		{
			if(++Count > MAX_NODES) { Error = "积木数量超过 256"; return false; }
			if(!CheckId(N.m_Id) || N.m_Kind < CScriptNode::K_ACTION || N.m_Kind > CScriptNode::K_WAIT || N.m_Block.m_Action < 0 || N.m_Block.m_Action >= NUM_ACTIONS || N.m_Block.m_Target < 0 || N.m_Block.m_Target >= NUM_TARGETS || N.m_Block.m_Value < 0 || N.m_Block.m_Value >= NUM_GETVALUES || N.m_Block.m_Argument.size() > 4096 || N.m_Count.size() > 4096 || !ValidUtf8(N.m_Block.m_Argument) || !ValidUtf8(N.m_Count) || !CheckConditions(N.m_vIfConds)) { Error = "积木数据或 ID 无效"; return false; }
			if(!N.HasChildren() && (!N.m_vThen.empty() || !N.m_vElse.empty())) { Error = "动作不能包含子积木"; return false; }
			if(N.m_Kind != CScriptNode::K_IF_ELSE && (!N.m_vElse.empty() || N.m_HasElse)) { Error = "只有 if 可包含 else"; return false; }
			if(!Check(N.m_vThen, Depth + 1) || !Check(N.m_vElse, Depth + 1)) return false;
		}
		return true;
	};
	for(const auto &R : Mod.m_vRules) if(!CheckId(R.m_Id) || R.m_Hook < 0 || R.m_Hook >= NUM_HOOKS || R.m_Argument.size() > 4096 || !ValidUtf8(R.m_Argument) || !CheckConditions(R.m_vConditions) || !Check(R.m_vBody, 1)) { if(Error.empty()) Error = "规则数据无效"; return false; }
	std::set<std::string> Names;
	for(const auto &V : Mod.m_vVars) if(!CheckId(V.m_Id) || V.m_Name.empty() || V.m_Name.size() > 128 || V.m_Initial.size() > 4096 || !ValidUtf8(V.m_Name) || !ValidUtf8(V.m_Initial) || !Names.insert(V.m_Name).second) { Error = "变量名或 ID 无效、重复"; return false; }
	for(const auto &P : Mod.m_vParams) if(!CheckId(P.m_Id) || P.m_Name.empty() || P.m_Name.size() > 128 || P.m_Value.size() > 4096 || !ValidUtf8(P.m_Name) || !ValidUtf8(P.m_Value) || !Names.insert(P.m_Name).second) { Error = "参数名与变量重复或无效"; return false; }
	Error.clear(); return true;
}
bool InsertNode(CMod &Mod, CScriptNode Node, const SSequenceTarget &Target, size_t Index, std::string &Error)
{
	CMod Copy = Mod; auto *pSeq = ResolveSequence(Copy, Target);
	if(!pSeq || Index > pSeq->size()) { Error = "目标序列不存在"; return false; }
	pSeq->insert(pSeq->begin() + static_cast<ptrdiff_t>(Index), std::move(Node));
	if(!ValidateMod(Copy, Error)) return false;
	Copy.Touch(); Mod = std::move(Copy); return true;
}
bool MoveNode(CMod &Mod, TId Source, const SSequenceTarget &Target, size_t Index, std::string &Error)
{
	CMod Copy = Mod; SSequenceTarget From; size_t FromIndex;
	if(!Locate(Copy, Source, From, FromIndex)) { Error = "源积木不存在"; return false; }
	auto *pFrom = ResolveSequence(Copy, From), *pTo = ResolveSequence(Copy, Target);
	if(!pTo || Index > pTo->size()) { Error = "目标序列不存在"; return false; }
	CScriptNode Node = (*pFrom)[FromIndex];
	if(Target.m_Parent && Contains(Node, Target.m_Parent)) { Error = "不能把积木放进自己或自己的子积木"; return false; }
	if(pFrom == pTo && (Index == FromIndex || Index == FromIndex + 1)) return true;
	if(pFrom == pTo && Index > FromIndex) --Index;
	pFrom->erase(pFrom->begin() + static_cast<ptrdiff_t>(FromIndex));
	pTo = ResolveSequence(Copy, Target); if(!pTo || Index > pTo->size()) { Error = "目标序列已失效"; return false; }
	pTo->insert(pTo->begin() + static_cast<ptrdiff_t>(Index), std::move(Node));
	if(!ValidateMod(Copy, Error)) return false;
	Copy.Touch(); Mod = std::move(Copy); return true;
}
bool DuplicateNode(CMod &Mod, TId Source, std::string &Error)
{
	SSequenceTarget From; size_t Index;
	if(!Locate(Mod, Source, From, Index)) { Error = "源积木不存在"; return false; }
	CScriptNode Copy = (*ResolveSequence(Mod, From))[Index];
	FreshIds(Copy);
	return InsertNode(Mod, std::move(Copy), From, Index + 1, Error);
}
bool DuplicateRule(CMod &Mod, TId Source, TId &NewRule, std::string &Error)
{
	auto It = std::find_if(Mod.m_vRules.begin(), Mod.m_vRules.end(), [Source](const CRule &Rule) { return Rule.m_Id == Source; });
	if(It == Mod.m_vRules.end()) { Error = "源规则不存在"; return false; }
	const size_t Index = static_cast<size_t>(It - Mod.m_vRules.begin());
	CRule Rule = *It;
	Rule.m_Id = NewId();
	for(auto &Condition : Rule.m_vConditions) Condition.m_Id = NewId();
	for(auto &Node : Rule.m_vBody) FreshIds(Node);
	CMod Copy = Mod;
	Copy.m_vRules.insert(Copy.m_vRules.begin() + static_cast<ptrdiff_t>(Index + 1), Rule);
	if(!ValidateMod(Copy, Error)) return false;
	NewRule = Rule.m_Id;
	Copy.Touch(); Mod = std::move(Copy);
	return true;
}
bool DeleteNode(CMod &Mod, TId Id) { for(auto &R : Mod.m_vRules) if(Erase(R.m_vBody, Id)) { Mod.Touch(); return true; } return false; }
std::string SaveMod(const CMod &Mod)
{
	std::ostringstream Out; Out << "format=2\nname=" << Quote(Mod.m_Name) << "\nenabled=" << Mod.m_Enabled << '\n';
	for(const auto &P : Mod.m_vParams) Out << "param " << Quote(P.m_Name) << ' ' << P.m_Numeric << ' ' << Quote(P.m_Value) << '\n';
	for(const auto &V : Mod.m_vVars) Out << "var " << Quote(V.m_Name) << ' ' << Quote(V.m_Initial) << '\n';
	for(const auto &R : Mod.m_vRules) { Out << "rule " << HOOK_KEYS[R.m_Hook] << ' ' << Quote(R.m_Argument) << '\n'; WriteConditions(Out, R.m_vConditions, 1); WriteNodes(Out, R.m_vBody, 1); Out << "endrule\n"; }
	return Out.str();
}
bool LoadMod(std::string_view Text, CMod &Mod, std::string &Error)
{
	if(Text.size() > 1024 * 1024 || !ValidUtf8(Text)) { Error = "文件过大、UTF-8 无效或包含 NUL"; return false; }
	if(Text.substr(0, 3) == "\xef\xbb\xbf") Text.remove_prefix(3);
	CMod Parsed; bool Modern = false; std::istringstream In{std::string(Text)}; std::string Line; size_t Number = 0; CRule *pRule = nullptr; int Nodes = 0;
	struct SFrame { CScriptNode *m_pNode; bool m_Else = false; };
	std::vector<SFrame> Stack;
	auto Fail = [&](const char *pMessage) { Error = "第 " + std::to_string(Number) + " 行: " + pMessage; return false; };
	auto Sequence = [&]() -> std::vector<CScriptNode> * { return Stack.empty() ? &pRule->m_vBody : Stack.back().m_Else ? &Stack.back().m_pNode->m_vElse : &Stack.back().m_pNode->m_vThen; };
	while(std::getline(In, Line))
	{
		++Number; if(!Line.empty() && Line.back() == '\r') Line.pop_back();
		size_t Start = Line.find_first_not_of(" \t"); if(Start == std::string::npos || Line[Start] == '#') continue;
		std::string_view S(Line); S.remove_prefix(Start);
		if(S == "format=2") { if(pRule) return Fail("格式声明必须在规则外"); Modern = true; continue; }
		if(S.substr(0, 5) == "name=") { if(pRule) return Fail("名称必须在规则外"); std::vector<std::string> T; if(Modern) { if(!Tokens(S.substr(5), T) || T.size() != 1) return Fail("名称无效"); Parsed.m_Name = T[0]; } else Parsed.m_Name = std::string(S.substr(5)); continue; }
		if(S.substr(0, 8) == "enabled=") { if(pRule || (S.substr(8) != "0" && S.substr(8) != "1")) return Fail("enabled 无效"); Parsed.m_Enabled = S.substr(8) == "1"; continue; }
		std::vector<std::string> T;
		// 旧版文本动作的剩余整行就是原文，不能把引号或 Windows 路径当转义解释。
		if(!Modern && S.size() > 3 && S.substr(0, 3) == "act" && (S[3] == ' ' || S[3] == '\t'))
		{
			size_t KeyBegin = S.find_first_not_of(" \t", 3);
			if(KeyBegin == std::string_view::npos) return Fail("缺少动作名");
			size_t KeyEnd = S.find_first_of(" \t", KeyBegin);
			T = {"act", std::string(S.substr(KeyBegin, KeyEnd == std::string_view::npos ? S.size() - KeyBegin : KeyEnd - KeyBegin))};
			if(KeyEnd != std::string_view::npos)
			{
				size_t Arg = S.find_first_not_of(" \t", KeyEnd);
				if(Arg != std::string_view::npos) T.push_back(std::string(S.substr(Arg)));
			}
		}
		else if(!Tokens(S, T) || T.empty()) return Fail("引号或转义无效");
		auto &W = T[0];
		if(W == "param") { if(pRule || T.size() != 4 || (T[2] != "0" && T[2] != "1")) return Fail("参数格式无效"); CModParam P; P.m_Name = T[1]; P.m_Numeric = T[2] == "1"; P.m_Value = T[3]; Parsed.m_vParams.push_back(std::move(P)); }
		else if(W == "var") { if(pRule || T.size() != 3) return Fail("变量格式无效"); CModVar V; V.m_Name = T[1]; V.m_Initial = T[2]; Parsed.m_vVars.push_back(std::move(V)); }
		else if(W == "rule") { if(pRule || T.size() < 2 || T.size() > 3 || Parsed.m_vRules.size() >= 64) return Fail("规则格式无效"); int H = KeyIndex(HOOK_KEYS, T[1]); if(H < 0) return Fail("未知事件"); Parsed.m_vRules.emplace_back(); pRule = &Parsed.m_vRules.back(); pRule->m_Hook = static_cast<EHook>(H); if(T.size() == 3) pRule->m_Argument = T[2]; }
		else if(W == "endrule") { if(!pRule || !Stack.empty() || T.size() != 1) return Fail("endrule 不匹配"); pRule = nullptr; }
		else if(!pRule) return Fail("积木必须在规则内");
		else if(W == "cond")
		{
			auto *pConditions = &pRule->m_vConditions;
			if(!Stack.empty())
			{
				if(Stack.back().m_pNode->m_Kind != CScriptNode::K_IF_ELSE || Stack.back().m_Else || !Stack.back().m_pNode->m_vThen.empty())
					return Fail("条件必须位于 if 的开头");
				pConditions = &Stack.back().m_pNode->m_vIfConds;
			}
			if(Modern)
			{
				CCondition Cond;
				if(!ReadCondition(T, 1, true, Cond))
					return Fail("条件格式无效");
				pConditions->push_back(std::move(Cond));
			}
			else if(T.size() < 2 || !ReadLegacyConditions(T, 1, *pConditions))
				return Fail("旧版条件格式无效");
		}
		else if(W == "else") { if(T.size() != 1 || Stack.empty() || Stack.back().m_pNode->m_Kind != CScriptNode::K_IF_ELSE || Stack.back().m_Else) return Fail("else 不匹配"); Stack.back().m_Else = true; Stack.back().m_pNode->m_HasElse = true; }
		else if(W == "endif" || W == "endrepeat") { if(T.size() != 1 || Stack.empty() || Stack.back().m_pNode->m_Kind != (W == "endif" ? CScriptNode::K_IF_ELSE : CScriptNode::K_LOOP)) return Fail("结束符不匹配"); Stack.pop_back(); }
		else
		{
			if(++Nodes > MAX_NODES || Stack.size() >= MAX_DEPTH) return Fail("积木数量或深度超过限制");
			CScriptNode N;
			if(W == "if")
			{
				N.m_Kind = CScriptNode::K_IF_ELSE;
				if(T.size() > 1 && (Modern || !ReadLegacyConditions(T, 1, N.m_vIfConds)))
					return Fail("内联条件无效");
			}
			else if(W == "repeat" || W == "wait") { if(T.size() != 2) return Fail("次数表达式无效"); N.m_Kind = W == "repeat" ? CScriptNode::K_LOOP : CScriptNode::K_WAIT; N.m_Count = T[1]; }
			else if(W == "act")
			{
				if(T.size() < 2)
					return Fail("缺少动作名");
				if(T[1] == "GetHookAimNearest" || T[1] == "AimNearest")
				{
					// Keep the rest editable, but never run a partially migrated rule.
					if(Modern && T.size() != 5) return Fail("动作格式无效");
					Parsed.m_RemovedAimActions = true;
					continue;
				}
				int A = KeyIndex(ACTION_KEYS, T[1]);
				if(A < 0)
					return Fail("未知动作");
				N.m_Block.m_Action = static_cast<EAction>(A);
				if(Modern) { if(T.size() != 5) return Fail("动作格式无效"); N.m_Block.m_Argument = T[2]; int Target = Int(T[3]), Value = Int(T[4]); if(Target < 0 || Target >= NUM_TARGETS || Value < 0 || Value >= NUM_GETVALUES) return Fail("目标或取值类型无效"); N.m_Block.m_Target = static_cast<ENearestTarget>(Target); N.m_Block.m_Value = static_cast<EGetValue>(Value); }
				else
				{
					auto Kind = ActionParamKind(N.m_Block.m_Action);
					if(ActionUsesGetValue(N.m_Block.m_Action) && T.size() > 2) { int V = Int(T[2]); if(V < 0 || V >= NUM_GETVALUES) return Fail("取值类型无效"); N.m_Block.m_Value = static_cast<EGetValue>(V); }
					else if(T.size() > 2)
					{
						size_t Pos = S.find(T[1]) + T[1].size(); Pos = S.find_first_not_of(" \t", Pos); N.m_Block.m_Argument = Pos == std::string_view::npos ? "" : std::string(S.substr(Pos));
						if(Kind == PARAM_INT) { auto Dollar = N.m_Block.m_Argument.find('$'); if(Dollar != std::string::npos) N.m_Block.m_Argument = N.m_Block.m_Argument.substr(Dollar); }
					}
				}
			}
			else return Fail("未知指令");
			auto *pSeq = Sequence(); pSeq->push_back(std::move(N)); if(pSeq->back().HasChildren()) Stack.push_back({&pSeq->back(), false});
		}
	}
	if(pRule || !Stack.empty()) return Fail("缺少结束符");
	if(!ValidateMod(Parsed, Error)) return false;
	if(Parsed.m_vRules.empty()) Parsed.m_vRules.emplace_back();
	if(Parsed.m_RemovedAimActions) Parsed.m_Enabled = false;
	Parsed.m_Filename = Mod.m_Filename; Parsed.m_DiskText = std::string(Text); Parsed.m_Dirty = Parsed.m_RemovedAimActions; Mod = std::move(Parsed); return true;
}
std::string SafeFilename(std::string_view Name)
{
	std::string Out;
	for(unsigned char C : Name) Out += (C < 32 || C == '<' || C == '>' || C == ':' || C == '"' || C == '/' || C == '\\' || C == '|' || C == '?' || C == '*') ? '_' : static_cast<char>(C);
	while(!Out.empty() && (Out.back() == '.' || Out.back() == ' ')) Out.pop_back();
	if(Out.size() > 128) { size_t End = 128; while(End && (static_cast<unsigned char>(Out[End]) & 0xc0) == 0x80) --End; Out.resize(End); }
	if(Out.empty() || Out == "." || Out == "..") Out = "mod";
	std::string Upper = Out; for(char &C : Upper) if(static_cast<unsigned char>(C) < 128) C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));
	std::string Stem = Upper.substr(0, Upper.find('.'));
	if(Stem == "CON" || Stem == "PRN" || Stem == "AUX" || Stem == "NUL" || (Stem.size() == 4 && (Stem.substr(0, 3) == "COM" || Stem.substr(0, 3) == "LPT") && Stem[3] >= '1' && Stem[3] <= '9')) Out = "_" + Out;
	return Out;
}
} // namespace modsystem
