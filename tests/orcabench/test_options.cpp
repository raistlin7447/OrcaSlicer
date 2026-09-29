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

TEST_CASE("an unknown argument is refused", "[OrcaBench][Options]")
{
    const std::string argument = GENERATE(as<std::string> {}, "--bogus", "list", "-l", "--list=1", "");
    CAPTURE(argument);
    CHECK_THROWS_AS(parse_options({argument}), OptionsError);
}

TEST_CASE("the usage names every option", "[OrcaBench][Options]")
{
    const std::string option = GENERATE(as<std::string> {}, "--list", "--help");
    CHECK_THAT(usage(), Catch::Matchers::ContainsSubstring(option));
}
