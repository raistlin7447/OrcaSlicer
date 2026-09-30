#include <catch2/catch_all.hpp>

#include "core/Sampler.hpp"
#include "core/Summary.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r::Bench;
using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

StageSpan span_of(std::string stage, std::string scope, Clock::duration at, Clock::duration length, Metrics metrics = {})
{
    StageSpan span;
    span.stage      = std::move(stage);
    span.scope      = std::move(scope);
    span.started_at = Clock::time_point {} + at;
    span.done_at    = span.started_at + length;
    span.metrics    = std::move(metrics);
    return span;
}

IterationResult iteration_of(Clock::duration wall, Timeline timeline)
{
    IterationResult iteration;
    iteration.wall     = wall;
    iteration.timeline = std::move(timeline);
    return iteration;
}

WorkloadResult ran(std::vector<IterationResult> iterations)
{
    return WorkloadResult::ran("slice/idler", std::nullopt, std::nullopt, std::move(iterations));
}

// A header whose operating system and thread count decide the CPU floor, on a machine of 20 cores.
Result header_on(std::string os, std::string threads = "20")
{
    Result header;
    header.machine.os                           = std::move(os);
    header.machine.logical_cores                = 20;
    header.measurement[MeasurementKey::threads] = std::move(threads);
    return header;
}

const Result on_linux = header_on("Linux 6.8.0");

const StageRow& row_of(const WorkloadSummary& summary, const std::string& stage, const std::string& scope = "")
{
    const auto found = std::find_if(summary.rows.begin(), summary.rows.end(),
                                    [&](const StageRow& row) { return row.stage == stage && row.scope == scope; });
    REQUIRE(found != summary.rows.end());
    return *found;
}

// Infill on two objects and the export, over two iterations.
WorkloadResult two_objects()
{
    return ran({iteration_of(120ms, {span_of("posInfill", "object:0", 0ms, 10ms), span_of("posInfill", "object:1", 0ms, 20ms),
                                     span_of("psGCodeExport", "print", 30ms, 70ms)}),
                iteration_of(120ms, {span_of("posInfill", "object:0", 0ms, 15ms), span_of("posInfill", "object:1", 0ms, 25ms),
                                     span_of("psGCodeExport", "print", 40ms, 60ms)})});
}

StageRow row_with(std::string stage, double share, StageState state = StageState::Ran)
{
    StageRow row;
    row.stage = std::move(stage);
    row.state = state;
    row.share = share;
    row.mean  = Millis(100 * share);
    return row;
}

std::vector<std::string> stages_of(const std::vector<StageRow>& rows)
{
    std::vector<std::string> stages;
    for (const StageRow& row : rows)
        stages.push_back(row.stage);
    return stages;
}

} // namespace

TEST_CASE("a row sums its stage across objects in each iteration, then takes the mean and min", "[OrcaBench][Summary]")
{
    const WorkloadSummary summary = summarize(two_objects(), on_linux, false);
    REQUIRE(summary.rows.size() == 2);
    CHECK(summary.summed_work == Millis(100));

    const StageRow& infill = row_of(summary, "posInfill");
    CHECK(infill.mean == Millis(35));
    CHECK(infill.min == Millis(30));
    REQUIRE(infill.cv.has_value());
    CHECK_THAT(*infill.cv, WithinRel(std::sqrt(50.0) / 35, 1e-12));
    CHECK_THAT(infill.share, WithinAbs(0.35, 1e-12));

    const StageRow& gcode = row_of(summary, "psGCodeExport");
    CHECK(gcode.mean == Millis(65));
    CHECK(gcode.min == Millis(60));
    CHECK_THAT(gcode.share, WithinAbs(0.65, 1e-12));
}

TEST_CASE("under --verbose each scope of a stage has its own row", "[OrcaBench][Summary]")
{
    const WorkloadSummary summary = summarize(two_objects(), on_linux, true);
    REQUIRE(summary.rows.size() == 3);
    CHECK(row_of(summary, "posInfill", "object:0").mean == Millis(12.5));
    CHECK(row_of(summary, "posInfill", "object:1").mean == Millis(22.5));
    CHECK(row_of(summary, "psGCodeExport", "print").mean == Millis(65));
}

TEST_CASE("a row has no CV with one iteration", "[OrcaBench][Summary]")
{
    const WorkloadSummary summary = summarize(ran({iteration_of(10ms, {span_of("posSlice", "object:0", 0ms, 5ms)})}), on_linux, false);
    CHECK_FALSE(row_of(summary, "posSlice").cv.has_value());
}

TEST_CASE("the wall envelope is the mean wall time, and unaccounted the time no span covers", "[OrcaBench][Summary]")
{
    // Two objects side by side, so the spans add up to more time than they cover.
    const Timeline spans {span_of("posInfill", "object:0", 0ms, 30ms), span_of("posPerimeters", "object:1", 10ms, 40ms),
                          span_of("psGCodeExport", "print", 60ms, 30ms)};
    const WorkloadSummary summary = summarize(ran({iteration_of(100ms, spans), iteration_of(120ms, spans)}), on_linux, false);
    CHECK(summary.summed_work == Millis(100));
    CHECK(summary.wall == Millis(110));
    CHECK(summary.unaccounted == Millis(30));
}

TEST_CASE("each stage is in one of four states", "[OrcaBench][Summary]")
{
    IterationResult iteration =
        iteration_of(10ms, {span_of("posSlice", "object:0", 0ms, 5ms), span_of("posContouring", "object:0", 5ms, 0ms)});
    iteration.unfinished      = {{"posEstimateCurledExtrusions", "object:0", Clock::time_point {} + 6ms}};
    iteration.not_run         = {{"posSimplifySupportPath", "object:0"}};
    const WorkloadSummary summary = summarize(ran({iteration}), on_linux, false);
    CHECK(row_of(summary, "posSlice").state == StageState::Ran);
    CHECK(row_of(summary, "posContouring").state == StageState::Instant);
    CHECK(row_of(summary, "posEstimateCurledExtrusions").state == StageState::Unfinished);
    CHECK(row_of(summary, "posSimplifySupportPath").state == StageState::NotRun);
}

TEST_CASE("a stage is instant only when every span of it took no time", "[OrcaBench][Summary]")
{
    const WorkloadSummary summary = summarize(ran({iteration_of(10ms, {span_of("posContouring", "object:0", 0ms, 1us)}),
                                                   iteration_of(10ms, {span_of("posContouring", "object:0", 0ms, 0ms)})}),
                                              on_linux, false);
    CHECK(row_of(summary, "posContouring").state == StageState::Ran);
}

TEST_CASE("a stage that ran in one scope is a stage that ran, though each scope keeps its own state", "[OrcaBench][Summary]")
{
    IterationResult iteration = iteration_of(10ms, {span_of("posSupportMaterial", "object:1", 0ms, 3ms)});
    iteration.not_run         = {{"posSupportMaterial", "object:0"}};
    const WorkloadResult workload = ran({iteration});

    const WorkloadSummary merged = summarize(workload, on_linux, false);
    CHECK(row_of(merged, "posSupportMaterial").state == StageState::Ran);
    CHECK(row_of(merged, "posSupportMaterial").mean == Millis(3));

    const WorkloadSummary verbose = summarize(workload, on_linux, true);
    CHECK(row_of(verbose, "posSupportMaterial", "object:0").state == StageState::NotRun);
    CHECK(row_of(verbose, "posSupportMaterial", "object:1").state == StageState::Ran);
}

TEST_CASE("a row's first start is its earliest offset from its iteration's origin", "[OrcaBench][Summary]")
{
    IterationResult first = iteration_of(200ms, {span_of("posSlice", "object:0", 0ms, 10ms), span_of("posInfill", "object:0", 30ms, 10ms)});
    first.not_run         = {{"posContouring", "object:0"}};
    IterationResult second =
        iteration_of(200ms, {span_of("posSlice", "object:0", 100ms, 10ms), span_of("posInfill", "object:0", 125ms, 10ms)});
    second.unfinished = {{"posEstimateCurledExtrusions", "object:0", Clock::time_point {} + 160ms}};
    second.not_run    = {{"posContouring", "object:0"}};
    const WorkloadSummary summary = summarize(ran({first, second}), on_linux, false);
    CHECK(row_of(summary, "posSlice").first_start == 0ms);
    CHECK(row_of(summary, "posInfill").first_start == 25ms);
    CHECK(row_of(summary, "posEstimateCurledExtrusions").first_start == 60ms);
    CHECK(row_of(summary, "posContouring").first_start == Clock::duration::max());
}

TEST_CASE("a row's CPU is its CPU time over its windows, and its peak its highest reading", "[OrcaBench][Summary]")
{
    const Metrics first  = {{SampledMetric::cpu_ns, 30e6}, {SampledMetric::cpu_window_ns, 10e6}, {SampledMetric::peak_rss_bytes, 500}};
    const Metrics second = {{SampledMetric::cpu_ns, 50e6}, {SampledMetric::cpu_window_ns, 10e6}, {SampledMetric::peak_rss_bytes, 700}};
    const WorkloadSummary summary = summarize(ran({iteration_of(20ms, {span_of("posInfill", "object:0", 0ms, 12ms, first)}),
                                                   iteration_of(20ms, {span_of("posInfill", "object:0", 0ms, 12ms, second)})}),
                                              on_linux, false);
    const StageRow& infill = row_of(summary, "posInfill");
    REQUIRE(infill.cpu.has_value());
    CHECK_THAT(*infill.cpu, WithinAbs(4.0, 1e-12));
    CHECK(infill.peak_rss_bytes == std::optional<std::uint64_t>(700));
}

TEST_CASE("a row without sampled readings has no CPU and no peak", "[OrcaBench][Summary]")
{
    const WorkloadSummary summary = summarize(ran({iteration_of(20ms, {span_of("posInfill", "object:0", 0ms, 12ms)})}), on_linux, false);
    const StageRow&       infill  = row_of(summary, "posInfill");
    CHECK_FALSE(infill.cpu.has_value());
    CHECK_FALSE(infill.cpu_below_floor);
    CHECK_FALSE(infill.peak_rss_bytes.has_value());
}

TEST_CASE("nothing is divided by a total of no time", "[OrcaBench][Summary]")
{
    SECTION("spans that all took no time give their rows no share")
    {
        const WorkloadSummary summary =
            summarize(ran({iteration_of(10ms, {span_of("posContouring", "object:0", 0ms, 0ms)})}), on_linux, false);
        CHECK(row_of(summary, "posContouring").share == 0.0);
    }
    SECTION("CPU time read over no time gives no CPU")
    {
        const Metrics         readings = {{SampledMetric::cpu_ns, 1e6}, {SampledMetric::cpu_window_ns, 0.0}};
        const WorkloadSummary summary =
            summarize(ran({iteration_of(10ms, {span_of("posInfill", "object:0", 0ms, 5ms, readings)})}), on_linux, false);
        CHECK_FALSE(row_of(summary, "posInfill").cpu.has_value());
    }
    SECTION("passes that took no time give the workload no CPU")
    {
        IterationResult iteration = iteration_of(0ms, {});
        iteration.cpu             = 5ms;
        CHECK_FALSE(summarize(ran({iteration}), on_linux, false).cpu.has_value());
    }
}

TEST_CASE("a row whose span overlaps a span outside it is marked shared", "[OrcaBench][Summary]")
{
    SECTION("spans of different stages side by side")
    {
        const WorkloadSummary summary = summarize(ran({iteration_of(100ms, {span_of("posInfill", "object:0", 0ms, 30ms),
                                                                            span_of("posPerimeters", "object:1", 10ms, 30ms),
                                                                            span_of("psGCodeExport", "print", 40ms, 20ms)})}),
                                                  on_linux, false);
        CHECK(row_of(summary, "posInfill").shared);
        CHECK(row_of(summary, "posPerimeters").shared);
        // It starts as posPerimeters ends, which is not an overlap.
        CHECK_FALSE(row_of(summary, "psGCodeExport").shared);
    }
    SECTION("one stage on two objects, which only --verbose splits")
    {
        const WorkloadResult workload =
            ran({iteration_of(100ms, {span_of("posInfill", "object:0", 0ms, 30ms), span_of("posInfill", "object:1", 5ms, 20ms)})});
        CHECK_FALSE(row_of(summarize(workload, on_linux, false), "posInfill").shared);
        CHECK(row_of(summarize(workload, on_linux, true), "posInfill", "object:0").shared);
    }
}

TEST_CASE("CPU recorded on Windows shows only when the tick's rounding is at most a tenth of it", "[OrcaBench][Summary]")
{
    // At 20 threads the rounding is 312.5 ms, and at one thread 15.625 ms.
    const auto [os, threads, cpu_ms, shown] = GENERATE(table<std::string, std::string, double, bool>({
        {"Windows 10.0.26200", "1", 200.0, true},
        {"Windows 10.0.26200", "1", 100.0, false},
        {"Windows 10.0.26200", "20", 4000.0, true},
        {"Windows 10.0.26200", "20", 2000.0, false},
        {"Windows 10.0.26200", "", 2000.0, false},
        {"Linux 6.8.0", "20", 1.0, true},
    }));
    CAPTURE(os, threads, cpu_ms);
    const Metrics readings = {{SampledMetric::cpu_ns, cpu_ms * 1e6}, {SampledMetric::cpu_window_ns, 1e9}};
    const WorkloadSummary summary =
        summarize(ran({iteration_of(2s, {span_of("posInfill", "object:0", 0ms, 1s, readings)})}), header_on(os, threads), false);
    const StageRow& infill = row_of(summary, "posInfill");
    CHECK(infill.cpu.has_value() == shown);
    CHECK(infill.cpu_below_floor == !shown);
}

TEST_CASE("a workload's CPU is its CPU time over its wall time, floored the same way", "[OrcaBench][Summary]")
{
    const auto run = [](Clock::duration first_cpu, Clock::duration second_cpu) {
        IterationResult first = iteration_of(1s, {});
        first.cpu             = first_cpu;
        IterationResult second = iteration_of(1s, {});
        second.cpu             = second_cpu;
        return ran({first, second});
    };
    const WorkloadSummary on_linux_summary = summarize(run(8s, 6s), on_linux, false);
    REQUIRE(on_linux_summary.cpu.has_value());
    CHECK_THAT(*on_linux_summary.cpu, WithinAbs(7.0, 1e-12));

    const Result windows = header_on("Windows 10.0.26200");
    CHECK(summarize(run(8s, 6s), windows, false).cpu.has_value());
    const WorkloadSummary floored = summarize(run(2s, 2s), windows, false);
    CHECK_FALSE(floored.cpu.has_value());
    CHECK(floored.cpu_below_floor);
}

TEST_CASE("a workload's peak memory is its highest iteration's", "[OrcaBench][Summary]")
{
    std::vector<IterationResult> iterations(3, iteration_of(1s, {}));
    iterations[0].peak_rss_bytes = 100;
    iterations[1].peak_rss_bytes = 300;
    iterations[2].peak_rss_bytes = 200;
    CHECK(summarize(ran(iterations), on_linux, false).peak_rss_bytes == std::optional<std::uint64_t>(300));
    CHECK_FALSE(summarize(ran({iteration_of(1s, {})}), on_linux, false).peak_rss_bytes.has_value());
}

TEST_CASE("a row under the threshold folds into the other row, and one at it stays", "[OrcaBench][Summary]")
{
    std::vector<StageRow>         rows {row_with("posSlice", 0.009), row_with("posPerimeters", 0.01), row_with("posInfill", 0.981)};
    const std::optional<OtherRow> other = collapse(rows, 0.01);
    REQUIRE(other.has_value());
    CHECK(other->stages == 1);
    CHECK(other->not_run == 0);
    CHECK_THAT(other->mean.count(), WithinAbs(0.9, 1e-12));
    CHECK_THAT(other->share, WithinAbs(0.009, 1e-12));
    CHECK(stages_of(rows) == std::vector<std::string> {"posPerimeters", "posInfill"});
}

TEST_CASE("the other row counts the stages that never ran", "[OrcaBench][Summary]")
{
    std::vector<StageRow> rows {row_with("posContouring", 0, StageState::NotRun),
                                row_with("posSimplifySupportPath", 0, StageState::Instant), row_with("posSlice", 0.005),
                                row_with("posInfill", 0.995)};
    const std::optional<OtherRow> other = collapse(rows, 0.01);
    REQUIRE(other.has_value());
    CHECK(other->stages == 3);
    CHECK(other->not_run == 1);
}

TEST_CASE("a stage that never finished and a significant row never fold", "[OrcaBench][Summary]")
{
    StageRow significant = row_with("posSlice", 0.002);
    significant.significant = true;
    std::vector<StageRow> rows {row_with("posEstimateCurledExtrusions", 0, StageState::Unfinished), significant,
                                row_with("posPerimeters", 0.002), row_with("posInfill", 0.996)};
    const std::optional<OtherRow> other = collapse(rows, 0.01);
    REQUIRE(other.has_value());
    CHECK(other->stages == 1);
    CHECK(stages_of(rows) == std::vector<std::string> {"posEstimateCurledExtrusions", "posSlice", "posInfill"});
}

TEST_CASE("no other row is made when nothing is under the threshold", "[OrcaBench][Summary]")
{
    std::vector<StageRow> rows {row_with("posInfill", 0.4), row_with("psGCodeExport", 0.6)};
    CHECK_FALSE(collapse(rows, 0.01).has_value());
    CHECK(rows.size() == 2);
}
