#include <tansr/json.hpp>

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using tansr::Json;
std::size_t checks = 0;
void check(bool condition, const std::string &label) {
    ++checks;
    if (!condition)
        throw std::runtime_error(label);
}
void throws(const std::function<void()> &action, const char *label) {
    try {
        action();
    } catch (const std::exception &) {
        check(true, label);
        return;
    }
    check(false, label);
}
Json parsed(std::string_view text, tansr::JsonLimits limits = {}) {
    auto result = Json::parse(text, limits);
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void strict_lexical_validation() {
    const std::vector<std::string> rejected = {R"({"a":1,"a":2})",
                                               R"({"a":1,"\u0061":2})",
                                               R"({"a":{"x":1,"x":2}})",
                                               R"({"s":"\ud800"})",
                                               R"("\udc00")",
                                               R"("\ud800\u0041")",
                                               R"("\ud800x")",
                                               "{} {}",
                                               "[01]",
                                               "[-01]",
                                               "[1.]",
                                               "[1e]",
                                               "[1e+]",
                                               "[+1]",
                                               "[.5]",
                                               "[NaN]",
                                               "[Infinity]",
                                               "[1,]",
                                               R"({"a":1,})",
                                               "\xef\xbb\xbf{}",
                                               "\"\xff\"",
                                               "\"\xc0\xaf\"",
                                               "\"\xed\xa0\x80\"",
                                               "truex",
                                               "",
                                               "\"raw\nline\"",
                                               "\"\xf4\x90\x80\x80\"",
                                               "\"\xf0\x80\x80\x80\"",
                                               "\"\xe2\x82\"",
                                               "{\"a\":1\v}",
                                               "/*x*/null",
                                               "\"\\x00\"",
                                               "\"\\uZZZZ\""};
    for (std::size_t i = 0; i < rejected.size(); ++i)
        check(!Json::parse(rejected[i]), "invalid ordinary JSON " + std::to_string(i));
    check(parsed(R"("\ud83d\ude00")").as_string() == "\xf0\x9f\x98\x80", "surrogate pair");
    check(parsed(R"("\u0000")").as_string() == std::string(1, '\0'), "embedded NUL");
    check(parsed(" \n\t\r null \t ").is_null(), "JSON whitespace");
    const auto duplicate = Json::parse(R"({"secret":1,"secret":2})");
    check(!duplicate && duplicate.error().message.find("duplicate_key") != std::string::npos,
          "duplicate category");
    check(duplicate.error().message.find("secret") == std::string::npos, "diagnostic omits input");
    check(tansr::valid_utf8("\xf4\x8f\xbf\xbf"), "last Unicode codepoint");
    check(!tansr::valid_utf8("\x80"), "stray continuation");
}
void owned_lexemes_and_order() {
    const std::string input =
        R"({"negativeZero":-0,"decimal":1.0,"exponent":1e0,"capital":1E+02,"huge":9007199254740993,"negative":-2.5,"infiniteFloat":1e400})";
    auto value = parsed(input);
    check(value.dump() == input, "ordinary tokens preserved");
    const std::vector<std::pair<std::string, std::string>> tokens = {
        {"negativeZero", "-0"},    {"decimal", "1.0"},           {"exponent", "1e0"},
        {"capital", "1E+02"},      {"huge", "9007199254740993"}, {"negative", "-2.5"},
        {"infiniteFloat", "1e400"}};
    for (const auto &token : tokens)
        check(value.at(token.first).number_token() == token.second, "number token");
    check(parsed(R"({"汉":1.2,"":-2})").at("汉").dump() == "1.2", "ordinary Unicode key");
    check(parsed(R"({"$serde_json::private::Number":"123"})").is_object(),
          "marker remains ordinary key");
    auto ordered = parsed(R"({"2":2,"a":null,"1":1,"b":[true]})");
    check(ordered.as_object()[0].first == "2" && ordered.as_object()[1].first == "a",
          "insertion order");
    check(ordered.contains("a") && ordered.at("a").is_null() && !ordered.contains("missing"),
          "null versus absent");
    auto independent = ordered;
    independent["b"].as_array()[0] = false;
    independent.set("2", 20);
    independent["new"] = "text";
    check(ordered["b"].at(std::size_t{0}).as_bool(), "deep copy array");
    check(ordered["2"].as_i64() == 2 && !ordered.contains("new"), "deep copy object");
    check(independent.as_object()[0].first == "2" && independent.as_object().back().first == "new",
          "replace preserves position");
    const auto moved = std::move(independent);
    check(moved.contains("new") && independent.is_null(), "move transfers owned AST");
    Json empty;
    empty["made"] = Json::array({1, true, nullptr});
    check(empty.dump() == R"({"made":[1,true,null]})", "object convenience methods");
    check(Json::object().dump() == "{}" && Json::array().dump() == "[]",
          "empty container distinction");
}
void numeric_access_and_invalid_ast() {
    check(parsed("18446744073709551615").as_u64() == std::numeric_limits<std::uint64_t>::max(),
          "uint64 max");
    check(parsed("-9223372036854775808").as_i64() == std::numeric_limits<std::int64_t>::min(),
          "int64 min");
    check(parsed("9223372036854775807").as_i64() == std::numeric_limits<std::int64_t>::max(),
          "int64 max");
    check(parsed("-0").as_i64() == 0, "signed negative zero access");
    throws([] { (void)parsed("18446744073709551616").as_u64(); }, "uint64 overflow");
    throws([] { (void)parsed("9223372036854775808").as_i64(); }, "int64 overflow");
    throws([] { (void)parsed("-1").as_u64(); }, "negative unsigned");
    throws([] { (void)parsed("1.0").as_i64(); }, "fraction token remains distinct");
    throws([] { (void)parsed("1e0").as_u64(); }, "exponent token remains distinct");
    throws([] { (void)Json(true).as_string(); }, "type check");
    throws([] { (void)Json::object().at("absent"); }, "missing key check");
    for (const auto *invalid : {"01", "-01", "+1", "1.", "1e", "NaN", "1 ", ""})
        check(!Json::number(invalid), "number factory grammar");
    check(Json::number("1e400").value().number_token() == "1e400",
          "unbounded ordinary numeric magnitude");
    throws([] { Json invalid(std::string("\xff")); }, "constructor UTF-8 check");
    throws([] { (void)Json::object({{"a", 1}, {"a", 2}}); }, "constructor duplicate check");
    auto changed = Json::object({{"a", 1}});
    changed.as_object().emplace_back("a", 2);
    throws([&] { (void)changed.dump(); }, "mutated duplicate rejected");
    auto string = Json("valid");
    string.as_string() = "\xff";
    throws([&] { (void)string.dump(); }, "mutated UTF-8 rejected");
}
void boundaries() {
    check(!Json::parse("null", {0, 32, 100000}), "zero byte cap");
    check(!Json::parse("null", {3, 32, 100000}), "byte cap exceeded");
    check(static_cast<bool>(Json::parse("null", {4, 32, 100000})), "exact byte cap");
    check(!Json::parse("null", {4, 32, 0}), "zero node cap");
    check(static_cast<bool>(Json::parse("0", {4, 0, 1})), "root at depth zero");
    check(!Json::parse("[0]", {4, 0, 2}), "child exceeds depth zero");
    const auto deep = std::string(32, '[') + "0" + std::string(32, ']');
    check(static_cast<bool>(Json::parse(deep)), "depth 32 accepted");
    check(!Json::parse("[" + deep + "]"), "depth 33 rejected");
    check(static_cast<bool>(Json::parse("[0,1]", {5, 32, 3})), "exact node cap");
    check(!Json::parse("[0,1]", {5, 32, 2}), "node cap exceeded");
    check(static_cast<bool>(Json::parse(R"({"key":0})", {9, 32, 2})), "keys not counted as nodes");
}
} // namespace

int main() {
    try {
        strict_lexical_validation();
        owned_lexemes_and_order();
        numeric_access_and_invalid_ast();
        boundaries();
        std::cout << "json: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "json failed: " << error.what() << '\n';
        return 1;
    }
}
