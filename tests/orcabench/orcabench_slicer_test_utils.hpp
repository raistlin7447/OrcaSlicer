#pragma once

#include "libslic3r/Utils.hpp"

#include <string>

namespace Slic3r { namespace Bench { namespace Test {

// The source tree's resources as resources_dir() for the scope's lifetime.
class TreeResources
{
public:
    TreeResources() : m_previous(resources_dir()) { set_resources_dir(ORCABENCH_RESOURCES_DIR); }
    ~TreeResources() { set_resources_dir(m_previous); }

    TreeResources(const TreeResources&)            = delete;
    TreeResources& operator=(const TreeResources&) = delete;

private:
    const std::string m_previous;
};

}}} // namespace Slic3r::Bench::Test
