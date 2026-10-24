#include "expression.h"
#include <charconv>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>

namespace modsystem
{
namespace
{
bool Ident(unsigned char C) { return std::isalnum(C) || C == '_' || C >= 128; }
struct SParser
{
	std::string_view m_Text;
	size_t m_Pos = 0;
	double m_Register;
	const FLookup &m_Lookup;
	std::string m_Error;
	void Space() { while(m_Pos < m_Text.size() && std::isspace(static_cast<unsigned char>(m_Text[m_Pos]))) ++m_Pos; }
	bool Fail(const char *pError) { if(m_Error.empty()) m_Error = pError; return false; }
	bool Atom(double &Out, int Depth)
	{
		if(Depth > 16) return Fail("表达式深度超过 16");
		Space();
		if(m_Pos == m_Text.size()) return Fail("缺少数值");
		char C = m_Text[m_Pos];
		if(C == '+' || C == '-') { ++m_Pos; if(!Atom(Out, Depth + 1)) return false; if(C == '-') Out = -Out; return true; }
		if(C == '(') { ++m_Pos; if(!Expression(Out, Depth + 1)) return false; Space(); if(m_Pos == m_Text.size() || m_Text[m_Pos++] != ')') return Fail("缺少右括号"); return true; }
		if(std::isdigit(static_cast<unsigned char>(C)) || C == '.')
		{
			const char *pStart = m_Text.data() + m_Pos;
			auto Result = std::from_chars(pStart, m_Text.data() + m_Text.size(), Out);
			if(Result.ec != std::errc() || !std::isfinite(Out)) return Fail("数值无效");
			m_Pos += Result.ptr - pStart;
			return true;
		}
		bool Percent = C == '%';
		if(Percent || C == '$') ++m_Pos;
		size_t Begin = m_Pos;
		while(m_Pos < m_Text.size() && Ident(static_cast<unsigned char>(m_Text[m_Pos]))) ++m_Pos;
		if(Begin == m_Pos) return Fail("变量名无效");
		auto Name = m_Text.substr(Begin, m_Pos - Begin);
		if(Percent && Name == "v")
		{
			Out = m_Register;
			// In %v%360, the second '%' is modulo, not a closing delimiter.
			if(m_Pos < m_Text.size() && m_Text[m_Pos] == '%')
			{
				size_t Next = m_Pos + 1;
				while(Next < m_Text.size() && std::isspace(static_cast<unsigned char>(m_Text[Next]))) ++Next;
				if(Next == m_Text.size() || m_Text[Next] == ')' || m_Text[Next] == '+' || m_Text[Next] == '-' || m_Text[Next] == '*' || m_Text[Next] == '/' || m_Text[Next] == '%') ++m_Pos;
			}
			return true;
		}
		if(Percent) { if(m_Pos == m_Text.size() || m_Text[m_Pos++] != '%') return Fail("变量占位符缺少 %"); }
		if(!m_Lookup || !m_Lookup(Name, Out) || !std::isfinite(Out)) return Fail("变量或参数不存在");
		return true;
	}
	bool Product(double &Out, int Depth)
	{
		if(!Atom(Out, Depth)) return false;
		for(;;)
		{
			Space(); if(m_Pos == m_Text.size()) break;
			char Op = m_Text[m_Pos]; if(Op != '*' && Op != '/' && Op != '%') break;
			++m_Pos; double R; if(!Atom(R, Depth)) return false;
			if(Op != '*' && R == 0) return Fail("除数不能为零");
			Out = Op == '*' ? Out * R : Op == '/' ? Out / R : std::fmod(Out, R);
			if(!std::isfinite(Out)) return Fail("计算溢出");
		}
		return true;
	}
	bool Expression(double &Out, int Depth)
	{
		if(!Product(Out, Depth)) return false;
		for(;;)
		{
			Space(); if(m_Pos == m_Text.size()) break;
			char Op = m_Text[m_Pos]; if(Op != '+' && Op != '-') break;
			++m_Pos; double R; if(!Product(R, Depth)) return false;
			Out = Op == '+' ? Out + R : Out - R;
			if(!std::isfinite(Out)) return Fail("计算溢出");
		}
		return true;
	}
};
}
bool SExprEval::Eval(std::string_view Expression, double Register, const FLookup &Lookup, double &Value, std::string *pError)
{
	if(Expression.size() > 4096) { if(pError) *pError = "表达式过长"; return false; }
	SParser Parser{Expression, 0, Register, Lookup, {}};
	double Result = 0;
	bool Ok = Parser.Expression(Result, 0);
	Parser.Space();
	if(Ok && Parser.m_Pos != Expression.size()) Ok = Parser.Fail("表达式含有多余字符");
	if(pError) *pError = Parser.m_Error;
	if(Ok) Value = Result;
	return Ok;
}
std::string FormatValue(double Value)
{
	std::ostringstream Stream;
	Stream.imbue(std::locale::classic());
	Stream << std::setprecision(15) << Value;
	return Stream.str();
}
std::string Substitute(std::string_view Text, double Register, const FLookup &Lookup, const std::function<bool(std::string_view, std::string &)> &TextLookup)
{
	std::string Out;
	for(size_t i = 0; i < Text.size() && Out.size() < 16384;)
	{
		if(Text[i] != '%') { Out += Text[i++]; continue; }
		if(Text.substr(i, 2) == "%v" && (i + 2 == Text.size() || !Ident(static_cast<unsigned char>(Text[i + 2]))))
		{
			Out += FormatValue(Register); i += 2; if(i < Text.size() && Text[i] == '%') ++i; continue;
		}
		size_t End = Text.find('%', i + 1);
		if(End == std::string_view::npos) { Out += Text[i++]; continue; }
		auto Name = Text.substr(i + 1, End - i - 1);
		double Number; std::string String;
		if(TextLookup && TextLookup(Name, String)) Out += String;
		else if(Lookup && Lookup(Name, Number)) Out += FormatValue(Number);
		else Out.append(Text.substr(i, End - i + 1));
		i = End + 1;
	}
	return Out;
}
bool SplitExpression(std::string_view Text, size_t &Begin, size_t &Operator, size_t &End)
{
	Begin = 0; End = Text.size();
	while(Begin < End && std::isspace(static_cast<unsigned char>(Text[Begin]))) ++Begin;
	while(End > Begin && std::isspace(static_cast<unsigned char>(Text[End - 1]))) --End;
	for(;;)
	{
		if(End - Begin < 2 || Text[Begin] != '(' || Text[End - 1] != ')') break;
		int Depth = 0; bool Whole = true;
		for(size_t i = Begin; i < End; ++i) { if(Text[i] == '(') ++Depth; if(Text[i] == ')') --Depth; if(Depth == 0 && i != End - 1) Whole = false; }
		if(!Whole) break;
		++Begin; --End;
	}
	int Depth = 0, BestPriority = 3;
	size_t Candidate = std::string_view::npos;
	bool ExpectOperand = true;
	for(size_t i = Begin; i < End; ++i)
	{
		char C = Text[i];
		if(std::isspace(static_cast<unsigned char>(C))) continue;
		if(C == '(') { ++Depth; continue; }
		if(C == ')') { --Depth; if(Depth < 0) return false; ExpectOperand = false; continue; }
		if(Depth != 0) continue;
		if(ExpectOperand && C == '%')
		{
			// %v is one operand; its next '%' can be a modulo operator.
			if(i + 1 < End && Text[i + 1] == 'v' && (i + 2 == End || !Ident(static_cast<unsigned char>(Text[i + 2]))))
			{
				++i;
				if(i + 1 < End && Text[i + 1] == '%')
				{
					size_t Next = i + 2;
					while(Next < End && std::isspace(static_cast<unsigned char>(Text[Next]))) ++Next;
					if(Next == End || Text[Next] == ')' || Text[Next] == '+' || Text[Next] == '-' || Text[Next] == '*' || Text[Next] == '/' || Text[Next] == '%') ++i;
				}
			}
			else
			{
				size_t Close = Text.find('%', i + 1);
				if(Close == std::string_view::npos || Close >= End) return false;
				i = Close;
			}
			ExpectOperand = false;
			continue;
		}
		if(C == '+' || C == '-' || C == '*' || C == '/' || C == '%')
		{
			if(ExpectOperand)
			{
				if(C == '+' || C == '-') continue;
				return false;
			}
			int Priority = C == '+' || C == '-' ? 1 : 2;
			if(Priority <= BestPriority) { BestPriority = Priority; Candidate = i; }
			ExpectOperand = true;
			continue;
		}
		// Skip a complete numeric literal so the sign in 1e-3 isn't a split.
		if(ExpectOperand && (std::isdigit(static_cast<unsigned char>(C)) || C == '.'))
		{
			double Value;
			auto Result = std::from_chars(Text.data() + i, Text.data() + End, Value);
			if(Result.ec != std::errc()) return false;
			i = static_cast<size_t>(Result.ptr - Text.data()) - 1;
		}
		ExpectOperand = false;
	}
	Operator = Candidate;
	return Depth == 0 && !ExpectOperand && Candidate != std::string_view::npos;
}
} // namespace modsystem
