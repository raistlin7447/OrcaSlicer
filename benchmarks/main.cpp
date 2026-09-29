#include "core/Options.hpp"
#include "core/Workload.hpp"

#include <exception>
#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
    using namespace Slic3r::Bench;
    try {
        WorkloadKinds::instance().require_registered();
        const Options options = parse_options(program_arguments(argc, argv));
        if (options.list && !options.help) {
            // No catalog file is read, so there is nothing to list.
            const std::vector<CatalogEntry> catalog;
            std::cout << listing(catalog);
            return 0;
        }
        std::cout << usage();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "orca_bench: " << error.what() << "\n" << usage();
        return 2;
    }
}
