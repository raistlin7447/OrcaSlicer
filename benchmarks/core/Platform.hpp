#pragma once

#include <cstdint>

namespace Slic3r { namespace Bench {

// Every query reports the current process, and answers 0 if the platform call fails.

// Resident memory held right now.
std::uint64_t current_rss_bytes();

// Highest resident memory since process start.
std::uint64_t peak_rss_bytes();

// User plus system CPU time consumed.
std::uint64_t process_cpu_ns();

}} // namespace Slic3r::Bench
