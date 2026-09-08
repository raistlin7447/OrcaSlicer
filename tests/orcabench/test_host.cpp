#include <catch2/catch_all.hpp>

#include "core/Host.hpp"

#include <cstdint>
#include <vector>

using namespace Slic3r::Bench;

// Assert relationships, never absolute times or sizes; those vary by machine and allocator.

TEST_CASE("resident memory is reported for a running process", "[OrcaBench][Host]")
{
    REQUIRE(current_rss_bytes() > 0);
}

TEST_CASE("resident memory grows when pages are actually touched", "[OrcaBench][Host]")
{
    // The compiler must assume a volatile is read, so a buffer whose address is stored here can be
    // neither dropped nor filled after the measurement that follows.
    static const unsigned char* volatile escaped = nullptr;

    const std::uint64_t before = current_rss_bytes();
    REQUIRE(before > 0);

    constexpr std::size_t megabytes = 64;
    const std::vector<unsigned char> ballast(megabytes * 1024 * 1024, 1);
    escaped = ballast.data();

    const std::uint64_t after = current_rss_bytes();
    escaped = nullptr;
    CHECK(after > before);
}

TEST_CASE("peak resident memory is never below the current figure", "[OrcaBench][Host]")
{
    const std::uint64_t current = current_rss_bytes();
    const std::uint64_t peak    = peak_rss_bytes();
    REQUIRE(peak > 0);
    CHECK(peak >= current);
}

TEST_CASE("process CPU time never moves backwards", "[OrcaBench][Host]")
{
    // On Windows the counter moves in ~15 ms ticks and reads 0 until one has passed. The
    // volatile sink keeps the loop that burns one from being optimized away.
    volatile std::uint64_t sink = 0;
    for (std::uint64_t i = 0; i < 50'000'000ull; ++i)
        sink += i * 2654435761ull;
    CHECK(sink != 0);

    // Without this, a stub returning 0 would satisfy monotonicity and pass.
    const std::uint64_t first = process_cpu_ns();
    REQUIRE(first > 0);
    CHECK(process_cpu_ns() >= first);
}
