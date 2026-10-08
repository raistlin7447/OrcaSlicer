#include "core/Compare.hpp"
#include "core/Document.hpp"
#include "core/Options.hpp"
#include "core/Reporters.hpp"
#include "core/Workload.hpp"

#include <exception>
#include <iostream>
#include <vector>

using namespace Slic3r::Bench;

namespace {

// Prints the comparison of two result documents, and returns 3 when a workload's output changed.
int compare_files(const CompareFiles& files, bool allow_mismatch)
{
    const Result     a          = read_document_file(files.a);
    const Result     b          = read_document_file(files.b);
    const Comparison comparison = compare(a, b, {allow_mismatch});
    CompareView      view;
    view.label_a = files.a;
    view.label_b = files.b;
    write_comparison(std::cout, comparison, view);
    return comparison.changed_outputs > 0 ? 3 : 0;
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
            return compare_files(*options.compare, options.allow_mismatch);
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
