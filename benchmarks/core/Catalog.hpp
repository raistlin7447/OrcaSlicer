#pragma once

#include "core/Workload.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r { namespace Bench {

// Thrown for a catalog that cannot be read.
class CatalogError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Reads a catalog, an object holding an entries array, refusing an entry validate() refuses, a field it
// does not know, so a misspelt one is never dropped, and a name that repeats.
std::vector<CatalogEntry> read_catalog(std::string_view text);

// Reads every .json file in the directory as one catalog, in file name order, naming the file in any
// error.
std::vector<CatalogEntry> read_catalog_dir(const std::string& directory);

}} // namespace Slic3r::Bench
