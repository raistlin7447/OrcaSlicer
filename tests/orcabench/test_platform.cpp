#include <catch2/catch_all.hpp>

#include "core/Platform.hpp"

#include <cstdint>
#include <vector>

using namespace Slic3r::Bench;

// Assert relationships, never absolute times or sizes; those vary by machine and allocator.

TEST_CASE("resident memory is reported for a running process", "[OrcaBench][Platform]")
{
    REQUIRE(current_rss_bytes() > 0);
}

TEST_CASE("resident memory grows when pages are actually touched", "[OrcaBench][Platform]")
{
    const std::uint64_t before = current_rss_bytes();
    REQUIRE(before > 0);

    // Untouched pages may never be committed, so this has to write, not just allocate.
    constexpr std::size_t   megabytes = 64;
    constexpr unsigned char marker    = 0xAB;
    std::vector<unsigned char> ballast(megabytes * 1024 * 1024);
    for (std::size_t i = 0; i < ballast.size(); i += 4096)
        ballast[i] = marker;

    const std::uint64_t after = current_rss_bytes();
    CHECK(after > before);

    // Proves the write happened, and keeps the buffer alive past the measurement.
    CHECK(ballast.front() == marker);
    CHECK(ballast.back() == 0);
}

TEST_CASE("peak resident memory is never below the current figure", "[OrcaBench][Platform]")
{
    const std::uint64_t current = current_rss_bytes();
    const std::uint64_t peak    = peak_rss_bytes();
    REQUIRE(peak > 0);
    CHECK(peak >= current);
}

TEST_CASE("process CPU time never moves backwards", "[OrcaBench][Platform]")
{
    // The counter has ~15 ms granularity, so a process that has not burned a full
    // tick legitimately reads 0. Hence real work first, and volatile so it stays.
    volatile std::uint64_t sink = 0;
    for (std::uint64_t i = 0; i < 50'000'000ull; ++i)
        sink += i * 2654435761ull;
    CHECK(sink != 0);

    // Without this, a stub returning 0 would satisfy monotonicity and pass.
    const std::uint64_t first = process_cpu_ns();
    REQUIRE(first > 0);
    CHECK(process_cpu_ns() >= first);
}
