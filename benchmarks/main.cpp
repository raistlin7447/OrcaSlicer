#include "core/Compare.hpp"
#include "core/Document.hpp"
#include "core/Host.hpp"
#include "core/Options.hpp"
#include "core/Reporters.hpp"
#include "core/Workload.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

using namespace Slic3r::Bench;

namespace {

// Prints the comparison of the two result documents the options name, and returns 3 when a workload's
// output changed.
int compare_files(const Options& options)
{
    const Result     a          = read_document_file(options.compare->a);
    const Result     b          = read_document_file(options.compare->b);
    const Comparison comparison = compare(a, b, {options.allow_mismatch});
    CompareView      view;
    view.label_a = options.compare->a;
    view.label_b = options.compare->b;
    view.color   = use_color(options.color, enable_terminal_escapes(), std::getenv("NO_COLOR"));
    write_comparison(std::cout, comparison, view);
    const bool changed = std::any_of(comparison.workloads.begin(), comparison.workloads.end(),
                                     [](const WorkloadComparison& workload) { return workload.output_changed; });
    return changed ? 3 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        WorkloadKinds::instance().require_registered();
        const Options options = parse_options(program_arguments(argc, argv));
        if (options.help) {
            std::cout << usage();
            return 0;
        }
        if (options.compare)
            return compare_files(options);
        if (options.list) {
            // No catalog file is read, so there is nothing to list.
            const std::vector<CatalogEntry> catalog;
            std::cout << listing(catalog);
            return 0;
        }
        std::cout << usage();
        return 0;
    } catch (const OptionsError& error) {
        std::cerr << "orca_bench: " << error.what() << "\n" << usage();
        return 2;
    } catch (const CompareError& error) {
        std::cerr << "orca_bench: " << error.what() << "\npass --allow-mismatch to compare them anyway\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "orca_bench: " << error.what() << "\n";
        return 1;
    }
}
