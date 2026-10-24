#include <game/client/components/tclient/modsystem/expression.h>
#include <game/client/components/tclient/modsystem/mod.h>
#include <game/client/components/tclient/modsystem/runtime.h>

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

namespace
{
namespace ms = modsystem;
ms::CMod TestMod()
{
	ms::CMod M; M.m_Name = "测试模组"; M.m_vRules.emplace_back(); return M;
}
ms::CScriptNode Act(ms::EAction Action, std::string Arg = {})
{
	ms::CScriptNode N; N.m_Block.m_Action = Action; N.m_Block.m_Argument = std::move(Arg); return N;
}
ms::CScriptNode Count(ms::CScriptNode::EKind Kind, std::string Expr)
{
	ms::CScriptNode N; N.m_Kind = Kind; N.m_Count = std::move(Expr); return N;
}
ms::CCondition Compare(ms::ECondition Type, std::string Left, std::string Right, ms::ELogic Logic = ms::LOGIC_AND, bool Not = false)
{
	ms::CCondition C; C.m_Type = Type; C.m_Left = std::move(Left); C.m_Right = std::move(Right); C.m_Logic = Logic; C.m_Not = Not; return C;
}
struct CTestHost : ms::IRuntimeHost
{
	bool m_Ground = true;
	int m_Actions = 0;
	std::vector<std::string> m_Output;
	bool Condition(const ms::CCondition &C, const ms::CMod &, ms::SRuntimeState &) override { return C.m_Type == ms::COND_IS_ON_GROUND && m_Ground; }
	void Action(const ms::CBlock &B, const ms::CMod &M, ms::SRuntimeState &S) override
	{
		++m_Actions;
		if(B.m_Action == ms::ACT_GET_VALUE) S.m_Register = 7;
		if(B.m_Action == ms::ACT_ECHO) m_Output.push_back(ms::ResolveText(M, S, B.m_Argument));
	}
};

TEST(ModSystem, ExpressionsRegisterModuloAndPrecedence)
{
	auto Lookup = [](std::string_view Name, double &Value) { if(Name == "角度" || Name == "n") { Value = 12; return true; } return false; };
	for(const auto &[Expr, Expected] : std::vector<std::pair<std::string, double>>{
		{"(%v + 角度 * 2) % 360", 14}, {"%v%360", 350}, {"%v% + 2", 352},
		{"%v % 360", 350}, {"%n% * 2 + $n", 36}, {"-(-2+4)*3", -6},
		{"1e-3+2", 2.001}, {"-7%3", -1}, {"(8/2)/2", 2}})
	{
		double Value = -999;
		ASSERT_TRUE(ms::SExprEval::Eval(Expr, 350, Lookup, Value)) << Expr;
		EXPECT_DOUBLE_EQ(Value, Expected) << Expr;
	}
}
TEST(ModSystem, ExpressionFailuresAndRecursionLimit)
{
	for(const auto &Expr : {"", "1/", "1%0", "(1+2", "1+2)", "1 2", "unknown", "1e309", "1e308*1e308"})
	{
		double Value = 123; std::string Error;
		EXPECT_FALSE(ms::SExprEval::Eval(Expr, 0, {}, Value, &Error)) << Expr;
		EXPECT_EQ(Value, 123); EXPECT_FALSE(Error.empty());
	}
	double Value = 0;
	EXPECT_TRUE(ms::SExprEval::Eval(std::string(16, '(') + "1" + std::string(16, ')'), 0, {}, Value));
	EXPECT_FALSE(ms::SExprEval::Eval(std::string(17, '(') + "1" + std::string(17, ')'), 0, {}, Value));
	EXPECT_FALSE(ms::SExprEval::Eval(std::string(4097, '1'), 0, {}, Value));
}
TEST(ModSystem, SplitPreservesOperandsAndUnarySigns)
{
	for(const auto &[Text, Expected] : std::vector<std::pair<std::string, char>>{
		{"(%v+5)", '+'}, {"(%v % 360)", '%'}, {"(%n% * 2)", '*'}, {"2*3-4", '-'},
		{"1e-3+2", '+'}, {"(1+-2)", '+'}, {"(5%-2)", '%'}, {"((1+2)*(3-4))", '*'}})
	{
		size_t B, Op, E; ASSERT_TRUE(ms::SplitExpression(Text, B, Op, E)) << Text;
		EXPECT_EQ(Text[Op], Expected) << Text; EXPECT_LT(B, Op); EXPECT_LT(Op + 1, E);
	}
	size_t B, Op, E;
	EXPECT_FALSE(ms::SplitExpression("%v", B, Op, E));
	EXPECT_FALSE(ms::SplitExpression("-2", B, Op, E));
	EXPECT_FALSE(ms::SplitExpression("%n%", B, Op, E));
}
TEST(ModSystem, Utf8StringsExpressionsEmptyElseRoundtrip)
{
	auto M = TestMod(); M.m_Name = "包含 空格的中文模组";
	ms::CModVar V; V.m_Name = "角度"; V.m_Initial = "(2 + 3) * 7"; M.m_vVars.push_back(V);
	ms::CModParam P; P.m_Name = "问候"; P.m_Numeric = false; P.m_Value = "你好\n\t\"世界\" \\"; M.m_vParams.push_back(P);
	ms::CScriptNode If; If.m_Kind = ms::CScriptNode::K_IF_ELSE; If.m_HasElse = true;
	If.m_vIfConds.push_back(Compare(ms::COND_VAR_EQ, "角度", "35", ms::LOGIC_OR, true));
	If.m_vThen.push_back(Act(ms::ACT_ECHO, "  你好 %问候% %v\n\\literal\\ \"quoted\"  "));
	auto Loop = Count(ms::CScriptNode::K_LOOP, "(角度 - 33)"); Loop.m_vThen.push_back(Count(ms::CScriptNode::K_WAIT, "%v + 1"));
	If.m_vThen.push_back(Loop); M.m_vRules[0].m_vBody.push_back(If);
	std::string Error, Text = ms::SaveMod(M); ms::CMod Loaded;
	ASSERT_TRUE(ms::LoadMod(Text, Loaded, Error)) << Error;
	const auto &Result = Loaded.m_vRules[0].m_vBody[0];
	EXPECT_TRUE(Result.m_HasElse); EXPECT_TRUE(Result.m_vElse.empty());
	EXPECT_EQ(Result.m_vThen[0].m_Block.m_Argument, If.m_vThen[0].m_Block.m_Argument);
	EXPECT_EQ(Loaded.m_vParams[0].m_Value, P.m_Value); EXPECT_EQ(ms::SaveMod(Loaded), Text);
}
TEST(ModSystem, EveryEnumRoundtrips)
{
	auto M = TestMod(); M.m_vRules.clear();
	for(int H = 0; H < ms::NUM_HOOKS; ++H)
	{
		ms::CRule R; R.m_Hook = static_cast<ms::EHook>(H); R.m_Argument = "space";
		for(int C = 0; C < ms::NUM_CONDITIONS; ++C) { ms::CCondition Cond; Cond.m_Type = static_cast<ms::ECondition>(C); R.m_vConditions.push_back(Cond); }
		for(int A = 0; A < ms::NUM_ACTIONS; ++A) R.m_vBody.push_back(Act(static_cast<ms::EAction>(A), "%v+3"));
		M.m_vRules.push_back(R);
	}
	std::string Error; ms::CMod Loaded;
	ASSERT_TRUE(ms::LoadMod(ms::SaveMod(M), Loaded, Error)) << Error;
	EXPECT_EQ(ms::SaveMod(M), ms::SaveMod(Loaded));
	EXPECT_EQ(ms::NUM_HOOKS, 12); EXPECT_EQ(ms::NUM_ACTIONS, 11); EXPECT_EQ(ms::NUM_CONDITIONS, 15);
}
TEST(ModSystem, LegacyMultipleConditionsComparisonAndDollarRoundtrip)
{
	const std::string Text = "name=旧版中文\nenabled=1\nparam bias 1 5\nvar angle 90\nrule OnTick\n"
		"cond HookIsReady VarEq angle:90\nif HookAimRight VarLt angle:100 VarGt angle 40 RandomProbability 25\n"
		"act SetHookAngle 0 $angle\nact Echo 中文  两个空格 %v %angle%\nelse\nendif\nendrule\n";
	ms::CMod M; std::string Error; ASSERT_TRUE(ms::LoadMod(Text, M, Error)) << Error;
	ASSERT_EQ(M.m_vRules[0].m_vConditions.size(), 2u);
	EXPECT_EQ(M.m_vRules[0].m_vConditions[1].m_Right, "90");
	const auto &If = M.m_vRules[0].m_vBody[0]; ASSERT_EQ(If.m_vIfConds.size(), 4u);
	EXPECT_EQ(If.m_vIfConds[1].m_Right, "100"); EXPECT_EQ(If.m_vIfConds[2].m_Right, "40"); EXPECT_EQ(If.m_vIfConds[3].m_Right, "25");
	EXPECT_EQ(If.m_vThen[0].m_Block.m_Argument, "$angle");
	EXPECT_EQ(If.m_vThen[1].m_Block.m_Argument, "中文  两个空格 %v %angle%");
	ms::CMod Reloaded; ASSERT_TRUE(ms::LoadMod(ms::SaveMod(M), Reloaded, Error)) << Error;
	EXPECT_EQ(ms::SaveMod(M), ms::SaveMod(Reloaded));
	ms::SRuntimeState S; S.m_Variables["angle"] = 90; double Value = 0;
	EXPECT_TRUE(ms::ResolveNumber(Reloaded, S, Reloaded.m_vRules[0].m_vBody[0].m_vThen[0].m_Block.m_Argument, Value)); EXPECT_EQ(Value, 90);
}
TEST(ModSystem, InvalidLegacyConditionsAreNotDiscarded)
{
	for(const auto &C : {"HookIsReady typo", "VarEq", "RandomProbability", "HookAimRight UnknownCondition"})
	{
		std::string Error; ms::CMod M;
		EXPECT_FALSE(ms::LoadMod(std::string("name=invalid\nrule OnTick\nif ") + C + "\nendif\nendrule\n", M, Error)) << C;
	}
}
TEST(ModSystem, ProbabilityMigratesOldEditorAndLegacyFiles)
{
	for(const auto &Text : {
		"format=2\nname=\"probability\"\nrule OnTick \"\"\ncond RandomProbability \"75\" \"0\" AND 0\nendrule\n",
		"format=2\nname=\"probability\"\nrule OnTick \"\"\ncond RandomProbability \"\" \"75\" AND 0\nendrule\n",
		"name=probability\nrule OnTick\ncond RandomProbability 75\nendrule\n"})
	{
		ms::CMod M, Reloaded; std::string Error;
		ASSERT_TRUE(ms::LoadMod(Text, M, Error)) << Error;
		ASSERT_EQ(M.m_vRules[0].m_vConditions.size(), 1u);
		EXPECT_TRUE(M.m_vRules[0].m_vConditions[0].m_Left.empty());
		EXPECT_EQ(M.m_vRules[0].m_vConditions[0].m_Right, "75");
		ASSERT_TRUE(ms::LoadMod(ms::SaveMod(M), Reloaded, Error)) << Error;
		EXPECT_EQ(ms::SaveMod(Reloaded), ms::SaveMod(M));
	}
}
TEST(ModSystem, ProbabilityEndpointsExpressionsInvalidNotAndDistribution)
{
	auto M = TestMod(); ms::SRuntimeState S; CTestHost Host;
	ms::CCondition C; C.m_Type = ms::COND_RANDOM_PROBABILITY;
	for(const char *pValue : {"0", "-50"})
	{
		C.m_Right = pValue;
		for(int i = 0; i < 20; ++i) EXPECT_FALSE(ms::EvaluateConditions({C}, M, S, Host));
	}
	for(const char *pValue : {"100", "150"})
	{
		C.m_Right = pValue;
		for(int i = 0; i < 20; ++i) EXPECT_TRUE(ms::EvaluateConditions({C}, M, S, Host));
	}
	S.m_Variables["chance"] = 50; C.m_Right = "chance * 2";
	EXPECT_TRUE(ms::EvaluateConditions({C}, M, S, Host));
	C.m_Right = "missing"; C.m_Not = true;
	EXPECT_FALSE(ms::EvaluateConditions({C}, M, S, Host));
	EXPECT_FALSE(S.m_Error.empty());
	C.m_Not = false; C.m_Right = "25";
	int Hits = 0;
	for(int i = 0; i < 20000; ++i) Hits += ms::EvaluateConditions({C}, M, S, Host);
	// Wide confidence interval, also excludes the Windows rand()%10000 bias.
	EXPECT_GT(Hits, 4500); EXPECT_LT(Hits, 5500);
}
TEST(ModSystem, LegacyTextKeepsQuotesBackslashesAndSpaces)
{
	const std::string Message = "\"C:\\mods\\测试 folder\"  unfinished \"quote %v";
	ms::CMod M; std::string Error;
	ASSERT_TRUE(ms::LoadMod("name=legacy\nrule OnTick\nact Echo " + Message + "\nendrule\n", M, Error)) << Error;
	EXPECT_EQ(M.m_vRules[0].m_vBody[0].m_Block.m_Argument, Message);
	ms::CMod Copy; ASSERT_TRUE(ms::LoadMod(ms::SaveMod(M), Copy, Error)) << Error;
	EXPECT_EQ(Copy.m_vRules[0].m_vBody[0].m_Block.m_Argument, Message);
}
TEST(ModSystem, RejectsInvalidUtf8AndAcceptsBomEmoji)
{
	ms::CMod M; std::string Error;
	for(const auto &Bad : {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4), std::string("\xe4\xb8", 2)})
		EXPECT_FALSE(ms::LoadMod("name=" + Bad + "\n", M, Error));
	ASSERT_TRUE(ms::LoadMod("\xef\xbb\xbfname=中文🧩\r\nrule OnTick\r\nact Echo 🧩\r\nendrule\r\n", M, Error)) << Error;
	EXPECT_EQ(M.m_Name, "中文🧩"); EXPECT_EQ(M.m_vRules[0].m_vBody[0].m_Block.m_Argument, "🧩");
}
TEST(ModSystem, MalformedLoadIsTransactional)
{
	auto M = TestMod(); M.m_vRules[0].m_vBody.push_back(Act(ms::ACT_ECHO, "keep"));
	const auto Original = ms::SaveMod(M); const auto Id = M.m_Id;
	for(const auto &Text : {"name=bad\nrule OnTick\nelse\nendrule\n", "name=bad\nrule unknown\nendrule\n",
		"name=bad\nrule OnTick\nif\nendrule\n", "format=2\nname=\"bad\"\nrule OnTick \"\"\nact Unknown \"\" 0 0\nendrule\n",
		"name=bad\nrule OnTick\nif\nelse\nelse\nendif\nendrule\n"})
	{
		std::string Error; EXPECT_FALSE(ms::LoadMod(Text, M, Error)); EXPECT_EQ(M.m_Id, Id);
		EXPECT_EQ(ms::SaveMod(M), Original); EXPECT_FALSE(Error.empty());
	}
}
TEST(ModSystem, EmptyRuleFallbackAndFileLimits)
{
	ms::CMod M; std::string Error; ASSERT_TRUE(ms::LoadMod("name=empty\nenabled=1\n", M, Error));
	ASSERT_EQ(M.m_vRules.size(), 1u); EXPECT_EQ(M.m_vRules[0].m_Hook, ms::HOOK_ON_TICK);
	EXPECT_FALSE(ms::LoadMod(std::string(1024 * 1024 + 1, 'x'), M, Error));
	EXPECT_FALSE(ms::LoadMod(std::string("name=x\0y\n", 9), M, Error));
	EXPECT_EQ(ms::SafeFilename("../bad:name\\file"), ".._bad_name_file");
	EXPECT_EQ(ms::SafeFilename("CON"), "_CON");
}
TEST(ModSystem, MoveSubtreeKeepsIdsAcrossSiblingShift)
{
	auto M = TestMod(); auto Rule = M.m_vRules[0].m_Id;
	auto A = Count(ms::CScriptNode::K_LOOP, "2"); A.m_vThen.push_back(Act(ms::ACT_ECHO, "child"));
	auto B = Count(ms::CScriptNode::K_LOOP, "1");
	const auto AId = A.m_Id, BId = B.m_Id, ChildId = A.m_vThen[0].m_Id; M.m_vRules[0].m_vBody = {A, B};
	std::string Error; ASSERT_TRUE(ms::MoveNode(M, AId, {Rule, BId, false}, 0, Error)) << Error;
	ASSERT_EQ(M.m_vRules[0].m_vBody.size(), 1u);
	EXPECT_EQ(M.m_vRules[0].m_vBody[0].m_Id, BId);
	EXPECT_EQ(M.m_vRules[0].m_vBody[0].m_vThen[0].m_Id, AId);
	EXPECT_EQ(M.m_vRules[0].m_vBody[0].m_vThen[0].m_vThen[0].m_Id, ChildId);
	const auto Before = ms::SaveMod(M);
	EXPECT_FALSE(ms::MoveNode(M, BId, {Rule, AId, false}, 0, Error)); EXPECT_EQ(ms::SaveMod(M), Before);
}
TEST(ModSystem, ElseInsertionAndInvalidTargetRollback)
{
	auto M = TestMod(); auto Rule = M.m_vRules[0].m_Id;
	ms::CScriptNode If; If.m_Kind = ms::CScriptNode::K_IF_ELSE; If.m_HasElse = true;
	auto Action = Act(ms::ACT_ECHO, "else"); M.m_vRules[0].m_vBody = {Action, If};
	std::string Error; ASSERT_TRUE(ms::MoveNode(M, Action.m_Id, {Rule, If.m_Id, true}, 0, Error));
	EXPECT_EQ(M.m_vRules[0].m_vBody[0].m_vElse[0].m_Id, Action.m_Id);
	const auto Before = ms::SaveMod(M);
	EXPECT_FALSE(ms::MoveNode(M, Action.m_Id, {Rule, 999999, false}, 0, Error)); EXPECT_EQ(ms::SaveMod(M), Before);
	EXPECT_TRUE(ms::DeleteNode(M, If.m_Id)); EXPECT_TRUE(M.m_vRules[0].m_vBody.empty());
}
TEST(ModSystem, StructuralDepthAndNodeBudget)
{
	auto M = TestMod(); std::string Error; auto N = Act(ms::ACT_ECHO);
	for(int i = 1; i < ms::MAX_DEPTH; ++i) { auto P = Count(ms::CScriptNode::K_LOOP, "1"); P.m_vThen.push_back(N); N = P; }
	M.m_vRules[0].m_vBody.push_back(N); EXPECT_TRUE(ms::ValidateMod(M, Error)) << Error;
	auto TooDeep = Count(ms::CScriptNode::K_LOOP, "1"); TooDeep.m_vThen.push_back(N); const auto Before = ms::SaveMod(M);
	EXPECT_FALSE(ms::InsertNode(M, TooDeep, {M.m_vRules[0].m_Id, 0, false}, 1, Error)); EXPECT_EQ(ms::SaveMod(M), Before);
	M.m_vRules[0].m_vBody.clear();
	for(int i = 0; i < ms::MAX_NODES; ++i) M.m_vRules[0].m_vBody.push_back(Act(ms::ACT_ECHO));
	EXPECT_TRUE(ms::ValidateMod(M, Error));
	EXPECT_FALSE(ms::InsertNode(M, Act(ms::ACT_ECHO), {M.m_vRules[0].m_Id, 0, false}, M.m_vRules[0].m_vBody.size(), Error));
	EXPECT_EQ(M.m_vRules[0].m_vBody.size(), static_cast<size_t>(ms::MAX_NODES));
}
TEST(ModSystem, AndOrNotPrecedence)
{
	auto M = TestMod(); ms::SRuntimeState S; S.m_Variables["x"] = 5; CTestHost Host;
	auto True = Compare(ms::COND_VAR_EQ, "x", "5"), False = Compare(ms::COND_VAR_GT, "x", "10");
	auto OrFalse = False; OrFalse.m_Logic = ms::LOGIC_OR;
	EXPECT_TRUE(ms::EvaluateConditions({}, M, S, Host)); EXPECT_FALSE(ms::EvaluateConditions({True, False}, M, S, Host));
	EXPECT_TRUE(ms::EvaluateConditions({True, OrFalse, False}, M, S, Host));
	auto Not = False; Not.m_Not = true; EXPECT_TRUE(ms::EvaluateConditions({Not}, M, S, Host));
	auto Missing = Compare(ms::COND_VAR_EQ, "missing", "5", ms::LOGIC_AND, true);
	EXPECT_FALSE(ms::EvaluateConditions({Missing}, M, S, Host));
}
TEST(ModSystem, InvalidModelNeverEntersExecutor)
{
	auto M = TestMod(); auto N = Act(ms::ACT_ECHO, "too deep");
	for(int i = 0; i < ms::MAX_DEPTH; ++i) { auto Parent = Count(ms::CScriptNode::K_LOOP, "1"); Parent.m_vThen.push_back(N); N = Parent; }
	M.m_vRules[0].m_vBody.push_back(N);
	std::vector<ms::CMod> Mods{M}; CTestHost Host; ms::CExecutor Exec;
	Exec.Fire(Mods, ms::HOOK_ON_TICK, {}, 0, Host);
	EXPECT_EQ(Exec.TaskCount(), 0u); EXPECT_TRUE(Host.m_Output.empty());
	ASSERT_NE(Exec.State(M.m_Id), nullptr); EXPECT_FALSE(Exec.State(M.m_Id)->m_Error.empty());
}
TEST(ModSystem, NumericTextParametersAndCycles)
{
	auto M = TestMod(); ms::SRuntimeState S;
	ms::CModParam A; A.m_Name = "a"; A.m_Value = "2+3";
	ms::CModParam B; B.m_Name = "b"; B.m_Value = "a*2";
	ms::CModParam Text; Text.m_Name = "问候"; Text.m_Numeric = false; Text.m_Value = "你好 世界";
	M.m_vParams = {A, B, Text}; S.m_Register = 7; double Value = 0;
	EXPECT_TRUE(ms::ResolveNumber(M, S, "b+%v", Value)); EXPECT_EQ(Value, 17);
	EXPECT_EQ(ms::ResolveText(M, S, "%问候% %a% %v %unknown%"), "你好 世界 5 7 %unknown%");
	M.m_vParams[0].m_Value = "b"; EXPECT_FALSE(ms::ResolveNumber(M, S, "a", Value));
}
TEST(ModSystem, WaitResumesChosenBranchAndLoop)
{
	auto M = TestMod(); ms::CModVar N; N.m_Name = "次数"; N.m_Initial = "2"; M.m_vVars.push_back(N);
	ms::CModParam Delay; Delay.m_Name = "delay"; Delay.m_Value = "2"; M.m_vParams.push_back(Delay);
	ms::CScriptNode If; If.m_Kind = ms::CScriptNode::K_IF_ELSE; If.m_HasElse = true;
	ms::CCondition Ground; Ground.m_Type = ms::COND_IS_ON_GROUND; If.m_vIfConds.push_back(Ground);
	auto Loop = Count(ms::CScriptNode::K_LOOP, "次数");
	Loop.m_vThen = {Act(ms::ACT_ECHO, "before"), Count(ms::CScriptNode::K_WAIT, "delay"), Act(ms::ACT_ECHO, "after")};
	If.m_vThen.push_back(Loop); If.m_vElse.push_back(Act(ms::ACT_ECHO, "else"));
	M.m_vRules[0].m_vBody = {If, Act(ms::ACT_ECHO, "done")};
	std::vector<ms::CMod> Mods{M}; CTestHost Host; ms::CExecutor Exec;
	Exec.Fire(Mods, ms::HOOK_ON_TICK, {}, 100, Host);
	EXPECT_EQ(Host.m_Output, std::vector<std::string>({"before"})); EXPECT_EQ(Exec.TaskCount(), 1u); Host.m_Ground = false;
	Exec.Advance(Mods, 101, Host); EXPECT_EQ(Host.m_Output.size(), 1u);
	Exec.Advance(Mods, 102, Host); EXPECT_EQ(Host.m_Output, std::vector<std::string>({"before", "after", "before"}));
	Exec.Advance(Mods, 104, Host); EXPECT_EQ(Host.m_Output, std::vector<std::string>({"before", "after", "before", "after", "done"}));
	EXPECT_EQ(Exec.TaskCount(), 0u);
}
TEST(ModSystem, WaitingOnTickHasOneActivationAndVariablesPersist)
{
	auto M = TestMod(); M.m_vRules[0].m_vBody = {Act(ms::ACT_GET_VALUE), Act(ms::ACT_STORE_VAR, "saved"), Count(ms::CScriptNode::K_WAIT, "3"), Act(ms::ACT_LOAD_VAR, "saved"), Act(ms::ACT_ECHO, "%v")};
	std::vector<ms::CMod> Mods{M}; CTestHost Host; ms::CExecutor Exec;
	for(int Tick = 0; Tick < 3; ++Tick) Exec.Fire(Mods, ms::HOOK_ON_TICK, {}, Tick, Host);
	EXPECT_EQ(Exec.TaskCount(), 1u); EXPECT_TRUE(Host.m_Output.empty());
	Exec.Advance(Mods, 3, Host); EXPECT_EQ(Host.m_Output, std::vector<std::string>({"7"}));
	ASSERT_NE(Exec.State(M.m_Id), nullptr); EXPECT_EQ(Exec.State(M.m_Id)->m_Variables.at("saved"), 7);
}
TEST(ModSystem, DisableRevisionDeleteResetCancelWaits)
{
	for(int Mode = 0; Mode < 4; ++Mode)
	{
		auto M = TestMod(); M.m_vRules[0].m_vBody = {Count(ms::CScriptNode::K_WAIT, "1"), Act(ms::ACT_ECHO, "must not run")};
		std::vector<ms::CMod> Mods{M}; CTestHost Host; ms::CExecutor Exec; Exec.Fire(Mods, ms::HOOK_ON_TICK, {}, 0, Host);
		if(Mode == 0) Mods[0].m_Enabled = false;
		if(Mode == 1) Mods[0].Touch();
		if(Mode == 2) Mods.clear();
		if(Mode == 3) Exec.Reset();
		Exec.Advance(Mods, 1, Host); EXPECT_EQ(Exec.TaskCount(), 0u); EXPECT_TRUE(Host.m_Output.empty()) << Mode;
	}
}
TEST(ModSystem, ActivationAndTickBudgetRemainBounded)
{
	auto M = TestMod(); auto Loop = Count(ms::CScriptNode::K_LOOP, "1000000"); Loop.m_vThen.push_back(Act(ms::ACT_ECHO, "bounded"));
	M.m_vRules[0].m_vBody = {Loop}; std::vector<ms::CMod> Mods{M}; CTestHost Host; ms::CExecutor Exec;
	Exec.Fire(Mods, ms::HOOK_ON_TICK, {}, 0, Host); EXPECT_LE(Host.m_Actions, ms::MAX_NODES);
	Exec.Advance(Mods, 1, Host); EXPECT_EQ(Exec.TaskCount(), 0u); EXPECT_LE(Host.m_Actions, ms::MAX_NODES);
	ASSERT_NE(Exec.State(M.m_Id), nullptr); EXPECT_FALSE(Exec.State(M.m_Id)->m_Error.empty());
}
TEST(ModSystem, KeyFilterAndReadonlyParameters)
{
	auto M = TestMod(); ms::CModParam P; P.m_Name = "angle"; P.m_Value = "30"; M.m_vParams.push_back(P);
	M.m_vRules[0].m_Hook = ms::HOOK_ON_KEY_PRESS; M.m_vRules[0].m_Argument = "space";
	M.m_vRules[0].m_vBody = {Act(ms::ACT_GET_VALUE), Act(ms::ACT_STORE_VAR, "angle"), Act(ms::ACT_LOAD_VAR, "angle"), Act(ms::ACT_ECHO, "%v")};
	std::vector<ms::CMod> Mods{M}; CTestHost Host; ms::CExecutor Exec;
	Exec.Fire(Mods, ms::HOOK_ON_KEY_PRESS, "a", 0, Host); EXPECT_TRUE(Host.m_Output.empty());
	Exec.Fire(Mods, ms::HOOK_ON_KEY_PRESS, "space", 0, Host); EXPECT_EQ(Host.m_Output, std::vector<std::string>({"30"}));
	EXPECT_EQ(Exec.State(M.m_Id)->m_Variables.count("angle"), 0u);
}

TEST(ModSystem, ExpressionAndSubstitution)
{
	double Value = 0;
	EXPECT_TRUE(modsystem::SExprEval::Eval("(2 + 3) * 4 % 6", 0, {}, Value));
	EXPECT_DOUBLE_EQ(Value, 2.0);
	EXPECT_FALSE(modsystem::SExprEval::Eval("1 / 0", 0, {}, Value));
	EXPECT_EQ(modsystem::Substitute("x=%v, y=%n%", 7, [](std::string_view Name, double &Out) { if(Name == "n") { Out = 9; return true; } return false; }, {}), "x=7, y=9");
}

TEST(ModSystem, SaveLoadPreservesTree)
{
	modsystem::CMod Mod;
	Mod.m_Name = "测试模组";
	Mod.m_vVars.push_back({});
	Mod.m_vVars.back().m_Name = "count";
	Mod.m_vVars.back().m_Initial = "3";
	modsystem::CScriptNode If;
	If.m_Kind = modsystem::CScriptNode::K_IF_ELSE;
	If.m_HasElse = true;
	If.m_vThen.push_back({});
	If.m_vThen.back().m_Block.m_Action = modsystem::ACT_ECHO;
	If.m_vThen.back().m_Block.m_Argument = "hello";
	Mod.m_vRules.emplace_back();
	Mod.m_vRules.front().m_vBody.push_back(If);
	const auto Text = modsystem::SaveMod(Mod);
	modsystem::CMod Loaded;
	std::string Error;
	ASSERT_TRUE(modsystem::LoadMod(Text, Loaded, Error)) << Error;
	ASSERT_EQ(Loaded.m_vRules.size(), 1u);
	ASSERT_EQ(Loaded.m_vRules.front().m_vBody.front().m_vThen.size(), 1u);
	EXPECT_TRUE(Loaded.m_vRules.front().m_vBody.front().m_HasElse);
}
}
