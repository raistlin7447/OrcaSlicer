#include "core/Options.hpp"

#include <algorithm>
#include <iterator>

namespace Slic3r { namespace Bench {

namespace {

ColorChoice color_choice(std::string_view value)
{
    if (value == "auto")
        return ColorChoice::Auto;
    if (value == "always")
        return ColorChoice::Always;
    if (value == "never")
        return ColorChoice::Never;
    throw OptionsError("--color needs auto|always|never, not '" + std::string(value) + "'");
}

struct Flag
{
    const char* name;
    // What the usage calls each value that follows the flag, one word per value.
    const char* values;
    void (*set)(Options& options, const std::string_view* values);
    const char* help;
};

// Every flag, which both the parser and the usage text read, so neither can miss one.
constexpr Flag flags[] = {
    {"--list", "", [](Options& options, const std::string_view*) { options.list = true; }, "print the catalog's workloads"},
    {"--policy", "name", [](Options& options, const std::string_view* values) { options.policy = std::string(values[0]); },
     "run the catalog under a policy, such as quick or precise"},
    {"--filter", "pattern", [](Options& options, const std::string_view* values) { options.filter = std::string(values[0]); },
     "take only the workloads whose names match, where * stands for any text"},
    {"--out", "file.json", [](Options& options, const std::string_view* values) { options.out = std::string(values[0]); },
     "write the run's result to the file"},
    {"--compare", "a.json b.json",
     [](Options& options, const std::string_view* values) {
         options.compare = CompareFiles {std::string(values[0]), std::string(values[1])};
     },
     "compare result a, from before a change, with result b, from after it"},
    {"--allow-mismatch", "", [](Options& options, const std::string_view*) { options.allow_mismatch = true; },
     "compare results that measured differently"},
    {"--color", "auto|always|never", [](Options& options, const std::string_view* values) { options.color = color_choice(values[0]); },
     "color the output, auto for a terminal unless TERM=dumb or NO_COLOR is non-empty"},
    {"--help", "", [](Options& options, const std::string_view*) { options.help = true; }, "print this text"},
};

std::size_t value_count(const Flag& flag)
{
    const std::string_view values = flag.values;
    return values.empty() ? 0 : 1 + static_cast<std::size_t>(std::count(values.begin(), values.end(), ' '));
}

std::string synopsis(const Flag& flag) { return value_count(flag) == 0 ? flag.name : std::string(flag.name) + " " + flag.values; }

} // namespace

std::vector<std::string_view> program_arguments(int argc, const char* const* argv)
{
    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.emplace_back(argv[i]);
    return arguments;
}

Options parse_options(const std::vector<std::string_view>& arguments)
{
    Options options;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string_view argument = arguments[i];
        const auto found = std::find_if(std::begin(flags), std::end(flags), [argument](const Flag& flag) { return argument == flag.name; });
        if (found == std::end(flags))
            throw OptionsError("unknown argument '" + std::string(argument) + "'");
        const std::size_t count = value_count(*found);
        // An argument starting with -- is a flag, never a value.
        for (std::size_t k = 1; k <= count; ++k)
            if (i + k >= arguments.size() || arguments[i + k].substr(0, 2) == "--")
                throw OptionsError(std::string(found->name) + " needs " + found->values);
        found->set(options, arguments.data() + i + 1);
        i += count;
    }
    if (options.allow_mismatch && !options.compare)
        throw OptionsError("--allow-mismatch needs --compare");
    if (options.filter && !options.policy && !options.list)
        throw OptionsError("--filter needs --policy or --list");
    if (options.out && !options.policy)
        throw OptionsError("--out needs --policy");
    if (options.policy && options.compare)
        throw OptionsError("--policy and --compare cannot run together");
    return options;
}

std::string usage()
{
    std::size_t widest = 0;
    for (const Flag& flag : flags)
        widest = std::max(widest, synopsis(flag).size());
    std::string text = "usage: orca_bench [options]\n";
    for (const Flag& flag : flags)
        text += "  " + synopsis(flag) + std::string(widest + 2 - synopsis(flag).size(), ' ') + flag.help + "\n";
    return text + "exit status: 0 done, 1 error or a failed workload, 2 bad command line, 3 --compare found changed output\n";
}

bool use_color(ColorChoice choice, bool terminal, const char* no_color, const char* term)
{
    if (choice == ColorChoice::Auto)
        return terminal && (no_color == nullptr || *no_color == '\0') && (term == nullptr || std::string_view(term) != "dumb");
    return choice == ColorChoice::Always;
}

}} // namespace Slic3r::Bench
