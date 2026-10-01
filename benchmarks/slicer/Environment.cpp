#include "slicer/Environment.hpp"

#include "libslic3r/Thread.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/log/core.hpp>
#include <tbb/task_arena.h>

#include <utility>

namespace Slic3r { namespace Bench {

SlicerEnvironment::SlicerEnvironment(std::string resources_dir) : m_resources_dir(std::move(resources_dir)) {}

void SlicerEnvironment::enter(const Policy& policy)
{
    // Made first, since it is the step that can fail, and leave() runs only after enter() returned.
    m_temporary_dir = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca_bench-%%%%-%%%%-%%%%");
    boost::filesystem::create_directories(m_temporary_dir);
    m_previous_temporary_dir = temporary_dir();
    set_temporary_dir(m_temporary_dir.string());

    m_previous_resources_dir = resources_dir();
    set_resources_dir(m_resources_dir);

    m_logging_was_enabled = boost::log::core::get()->get_logging_enabled();
    boost::log::core::get()->set_logging_enabled(false);

    // The first Print::process() names the pool's threads by waiting until every one of them runs, which
    // a cap below the arena's size would hold back forever, so the naming comes before the cap.
    name_tbb_thread_pool_threads_set_locale();
    m_thread_cap.emplace(tbb::global_control::max_allowed_parallelism, policy.threads);
}

void SlicerEnvironment::leave() noexcept
{
    m_thread_cap.reset();
    boost::log::core::get()->set_logging_enabled(m_logging_was_enabled);
    set_resources_dir(m_previous_resources_dir);
    set_temporary_dir(m_previous_temporary_dir);
    boost::system::error_code ignored;
    boost::filesystem::remove_all(m_temporary_dir, ignored);
}

unsigned hardware_threads() { return unsigned(tbb::this_task_arena::max_concurrency()); }

}} // namespace Slic3r::Bench
