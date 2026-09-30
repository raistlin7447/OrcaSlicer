#include <catch2/catch_all.hpp>

#include "core/Options.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace Slic3r::Bench;

TEST_CASE("a command line without a program name has no arguments", "[OrcaBench][Options]")
{
    const char* const only_program[] = {"orca_bench"};
    CHECK(program_arguments(0, nullptr).empty());
    CHECK(program_arguments(1, only_program).empty());
}

TEST_CASE("the arguments are the ones after the program name", "[OrcaBench][Options]")
{
    const char* const command_line[] = {"orca_bench", "--list", "--help"};
    CHECK(program_arguments(3, command_line) == std::vector<std::string_view> {"--list", "--help"});
}

TEST_CASE("no arguments ask for nothing", "[OrcaBench][Options]")
{
    const Options options = parse_options({});
    CHECK_FALSE(options.help);
    CHECK_FALSE(options.list);
    CHECK_FALSE(options.compare);
    CHECK_FALSE(options.allow_mismatch);
}

TEST_CASE("the list flag asks for the catalog", "[OrcaBench][Options]")
{
    const Options options = parse_options({"--list"});
    CHECK(options.list);
    CHECK_FALSE(options.help);
}

TEST_CASE("the help flag asks for the usage", "[OrcaBench][Options]")
{
    const Options options = parse_options({"--help"});
    CHECK(options.help);
    CHECK_FALSE(options.list);
}

TEST_CASE("the compare flag takes the results from before and after a change", "[OrcaBench][Options]")
{
    const Options options = parse_options({"--compare", "before.json", "after.json", "--allow-mismatch"});
    REQUIRE(options.compare);
    CHECK(options.compare->a == "before.json");
    CHECK(options.compare->b == "after.json");
    CHECK(options.allow_mismatch);
}

TEST_CASE("the compare flag without two results after it is refused", "[OrcaBench][Options]")
{
    const std::vector<std::string_view> arguments = GENERATE(values<std::vector<std::string_view>>({
        {"--compare"},
        {"--compare", "a.json"},
        {"--compare", "a.json", "--allow-mismatch"},
        {"--compare", "--list", "b.json"},
    }));
    CAPTURE(arguments);
    CHECK_THROWS_WITH(parse_options(arguments), "--compare needs a.json b.json");
}

TEST_CASE("the allow-mismatch flag without the compare flag is refused", "[OrcaBench][Options]")
{
    CHECK_THROWS_WITH(parse_options({"--allow-mismatch"}), "--allow-mismatch needs --compare");
    CHECK_THROWS_WITH(parse_options({"--list", "--allow-mismatch"}), "--allow-mismatch needs --compare");
}

TEST_CASE("an unknown argument is refused", "[OrcaBench][Options]")
{
    const std::string argument = GENERATE(as<std::string> {}, "--bogus", "list", "-l", "--list=1", "");
    CAPTURE(argument);
    CHECK_THROWS_AS(parse_options({argument}), OptionsError);
}

TEST_CASE("the usage lists each option with its values, then the exit statuses", "[OrcaBench][Options]")
{
    CHECK(usage() == "usage: orca_bench [options]\n"
                     "  --list                   print the catalog's workloads\n"
                     "  --compare a.json b.json  compare result a, from before a change, with result b, from after it\n"
                     "  --allow-mismatch         compare results that measured differently\n"
                     "  --help                   print this text\n"
                     "exit status: 0 done, 1 error, 2 bad command line, 3 --compare found changed output\n");
}
