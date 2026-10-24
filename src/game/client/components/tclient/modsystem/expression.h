#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_EXPRESSION_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_EXPRESSION_H
#include <functional>
#include <string>
#include <string_view>
namespace modsystem
{
using FLookup = std::function<bool(std::string_view, double &)>;
struct SExprEval
{
	static bool Eval(std::string_view Expression, double Register, const FLookup &Lookup, double &Value, std::string *pError = nullptr);
};
std::string FormatValue(double Value);
std::string Substitute(std::string_view Text, double Register, const FLookup &Lookup, const std::function<bool(std::string_view, std::string &)> &TextLookup = {});
// Returns top-level binary expression spans, for recursively editable operands.
bool SplitExpression(std::string_view Text, size_t &Begin, size_t &Operator, size_t &End);
} // namespace modsystem
#endif
