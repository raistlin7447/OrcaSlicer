#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r { namespace Bench {

// Thrown for a command line orca_bench cannot use.
class OptionsError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// What the command line asks for.
struct Options
{
    bool help = false;
    bool list = false;
};

// The arguments after the program name, and none when argc is 0, which an exec with an empty argv
// allows.
std::vector<std::string_view> program_arguments(int argc, const char* const* argv);

// Reads the arguments that follow the program name.
Options parse_options(const std::vector<std::string_view>& arguments);

// What --help prints, one line per option.
std::string usage();

}} // namespace Slic3r::Bench
