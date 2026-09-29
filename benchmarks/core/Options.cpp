#include "core/Options.hpp"

#include <algorithm>
#include <iterator>

namespace Slic3r { namespace Bench {

namespace {

struct Flag
{
    const char* name;
    bool Options::*field;
    const char* help;
};

// Every flag, which both the parser and the usage text read, so neither can miss one.
constexpr Flag flags[] = {
    {"--list", &Options::list, "print the catalog's workloads"},
    {"--help", &Options::help, "print this text"},
};

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
    for (const std::string_view argument : arguments) {
        const auto found = std::find_if(std::begin(flags), std::end(flags), [argument](const Flag& flag) { return argument == flag.name; });
        if (found == std::end(flags))
            throw OptionsError("unknown argument '" + std::string(argument) + "'");
        options.*(found->field) = true;
    }
    return options;
}

std::string usage()
{
    std::string text = "usage: orca_bench [options]\n";
    for (const Flag& flag : flags)
        text += "  " + std::string(flag.name) + "  " + flag.help + "\n";
    return text;
}

}} // namespace Slic3r::Bench
