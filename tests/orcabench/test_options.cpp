#include <catch2/catch_all.hpp>

#include "core/Options.hpp"

#include <optional>
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
    CHECK_FALSE(options.policy);
    CHECK_FALSE(options.filter);
    CHECK_FALSE(options.out);
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

TEST_CASE("the policy, filter and out flags ask for a run of the workloads the pattern selects, and where its result goes",
          "[OrcaBench][Options]")
{
    const Options options = parse_options({"--policy", "quick", "--filter", "slice/3dbenchy/*", "--out", "run.json"});
    CHECK(options.policy == std::optional<std::string>("quick"));
    CHECK(options.filter == std::optional<std::string>("slice/3dbenchy/*"));
    CHECK(options.out == std::optional<std::string>("run.json"));
    CHECK(parse_options({"--list", "--filter", "slice/*"}).filter == std::optional<std::string>("slice/*"));
}

TEST_CASE("a filter needs a run or a listing, an out file needs a run, and a run cannot also compare", "[OrcaBench][Options]")
{
    const auto [arguments, refusal] = GENERATE(table<std::vector<std::string_view>, std::string>({
        {{"--filter", "slice/*"}, "--filter needs --policy or --list"},
        {{"--out", "run.json"}, "--out needs --policy"},
        {{"--list", "--out", "run.json"}, "--out needs --policy"},
        {{"--policy", "quick", "--compare", "a.json", "b.json"}, "--policy and --compare cannot run together"},
    }));
    CAPTURE(arguments);
    CHECK_THROWS_WITH(parse_options(arguments), refusal);
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
                     "  --list                     print the catalog's workloads\n"
                     "  --policy name              run the catalog under a policy, such as quick or precise\n"
                     "  --filter pattern           take only the workloads whose names match, where * stands for any text\n"
                     "  --out file.json            write the run's result to the file\n"
                     "  --compare a.json b.json    compare result a, from before a change, with result b, from after it\n"
                     "  --allow-mismatch           compare results that measured differently\n"
                     "  --color auto|always|never  color the output, auto for a terminal unless TERM=dumb or NO_COLOR is non-empty\n"
                     "  --help                     print this text\n"
                     "exit status: 0 done, 1 error or a failed workload, 2 bad command line, 3 --compare found changed output\n");
}

TEST_CASE("the color flag says when to color the output", "[OrcaBench][Options]")
{
    CHECK(parse_options({}).color == ColorChoice::Auto);
    CHECK(parse_options({"--color", "always"}).color == ColorChoice::Always);
    CHECK(parse_options({"--color", "never"}).color == ColorChoice::Never);
    CHECK(parse_options({"--color", "auto"}).color == ColorChoice::Auto);
    CHECK_THROWS_WITH(parse_options({"--color", "sometimes"}), "--color needs auto|always|never, not 'sometimes'");
    CHECK_THROWS_WITH(parse_options({"--color"}), "--color needs auto|always|never");
}

TEST_CASE("auto colors a terminal unless NO_COLOR holds something or TERM is dumb, and always and never decide alone",
          "[OrcaBench][Options]")
{
    const auto [choice, terminal, no_color, term, colored] = GENERATE(table<ColorChoice, bool, const char*, const char*, bool>({
        {ColorChoice::Auto, true, nullptr, nullptr, true},
        {ColorChoice::Auto, true, "", "xterm-256color", true},
        {ColorChoice::Auto, true, "1", nullptr, false},
        {ColorChoice::Auto, true, nullptr, "dumb", false},
        {ColorChoice::Auto, false, nullptr, nullptr, false},
        {ColorChoice::Always, false, "1", "dumb", true},
        {ColorChoice::Never, true, nullptr, nullptr, false},
    }));
    CHECK(use_color(choice, terminal, no_color, term) == colored);
}
