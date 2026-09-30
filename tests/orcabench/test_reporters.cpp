#include <catch2/catch_all.hpp>

#include "core/Document.hpp"
#include "core/Reporters.hpp"
#include "core/Sampler.hpp"
#include "orcabench_test_utils.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r::Bench;
using namespace Slic3r::Bench::Test;
using namespace std::chrono_literals;

namespace {

constexpr std::uint64_t mib = 1024 * 1024;

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

Metrics sampled(double cpu_ms, double window_ms, std::uint64_t peak_mib)
{
    return {{SampledMetric::cpu_ns, cpu_ms * 1e6},
            {SampledMetric::cpu_window_ns, window_ms * 1e6},
            {SampledMetric::peak_rss_bytes, double(peak_mib * mib)}};
}

// One workload of each outcome, the one that ran over two iterations with a stage in each state.
Result canned()
{
    Result result;
    result.suite.duration = 6100ms;
    result.measurement    = {{MeasurementKey::policy, "quick"},
                             {MeasurementKey::warmup, "1"},
                             {MeasurementKey::iterations, "2"},
                             {MeasurementKey::threads, "20"}};
    result.build.revision         = "a83e6b7b74";
    result.build.dirty            = true;
    result.build.compiler         = "Clang";
    result.build.compiler_version = "22.1.3";
    result.build.config           = "Release";
    result.build.flags            = "/O2 /Ob2 /DNDEBUG";
    result.machine.host           = "BENCH-PC";
    result.machine.os             = "Linux 6.8.0";
    result.machine.cpu            = "13th Gen Intel(R) Core(TM) i5-13600K";
    result.machine.logical_cores  = 20;

    IterationResult first;
    first.wall           = 1000ms;
    first.cpu            = 7000ms;
    first.peak_rss_bytes = 400 * mib;
    first.timeline       = {span_of("posSlice", "object:0", 0ms, 12ms),
                            span_of("posInfill", "object:0", 20ms, 90ms, sampled(810, 90, 405)),
                            span_of("posSimplifyInfill", "object:0", 110ms, 5ms),
                            span_of("posContouring", "object:0", 115ms, 0ms),
                            span_of("psGCodeExport", "print", 150ms, 800ms, sampled(4000, 800, 400))};
    first.unfinished     = {{"posEstimateCurledExtrusions", "object:0", Clock::time_point {} + 120ms}};
    first.not_run        = {{"posSimplifySupportPath", "object:0"}};

    IterationResult second;
    second.wall           = 1030ms;
    second.cpu            = 7210ms;
    second.peak_rss_bytes = 420 * mib;
    second.timeline       = {span_of("posSlice", "object:0", 0ms, 13ms),
                             span_of("posInfill", "object:0", 20ms, 96ms, sampled(864, 96, 410)),
                             span_of("posSimplifyInfill", "object:0", 116ms, 6ms),
                             span_of("posContouring", "object:0", 122ms, 0ms),
                             span_of("psGCodeExport", "print", 160ms, 810ms, sampled(4050, 810, 420))};
    second.unfinished = {{"posEstimateCurledExtrusions", "object:0", Clock::time_point {} + 125ms}};
    second.not_run    = {{"posSimplifySupportPath", "object:0"}};

    WorkStats work;
    work.moves       = 606972;
    work.layers      = 190;
    result.workloads = {WorkloadResult::ran("slice/extruder-idler/standard-0.20", 0x3bbecdbaa64c8fe8, work, {first, second}),
                        WorkloadResult::skipped("slice/benchy/standard-0.20", "fixture not available: corpus 'external' not fetched"),
                        WorkloadResult::failed("slice/overhangs/tree-supports", "threw std::bad_alloc in posSupportMaterial")};
    return result;
}

std::string render(const std::string& name, const Result& result, const ReportOptions& options = {})
{
    std::ostringstream out;
    report(*make_reporter(name, out, options), result);
    return out.str();
}

const char* const canned_header = "orca_bench  quick  threads=20  Clang 22.1.3  Release  a83e6b7b74 dirty\n"
                                  "BENCH-PC  Linux 6.8.0  13th Gen Intel(R) Core(TM) i5-13600K  20 cores\n";

const char* const skipped_block = "\n"
                                  "slice/benchy/standard-0.20                                               SKIPPED\n"
                                  "  fixture not available: corpus 'external' not fetched\n";

} // namespace

TEST_CASE("the console lays out a run's rows, totals and outcomes", "[OrcaBench][Reporters]")
{
    CHECK(render("console", canned()) ==
          std::string(canned_header) +
              "\n"
              "slice/extruder-idler/standard-0.20                       1 warmup + 2 iterations\n"
              "                                   mean     min     CV   share     CPU  peak MiB\n"
              "  psGCodeExport                   805.0   800.0   0.9%   87.9%    5.0x       420\n"
              "  posInfill                        93.0    90.0   4.6%~  10.2%    9.0x       410\n"
              "  posSlice                         12.5    12.0   5.7%~   1.4%       -         -\n"
              "  posEstimateCurledExtrusions  never finished\n"
              "  other (3 stages, 1 not run)       5.5       -      -    0.6%\n"
              "  ------------------------------------------------------------------------------\n"
              "  summed work                     916.0\n"
              "  wall envelope                  1015.0\n"
              "  unaccounted                      99.0                   9.8%\n"
              "\n"
              "  peak RSS 420 MiB   CPU 7.0x   moves 606,972   layers 190   hash 3bbecdba\n" +
              skipped_block +
              "\n"
              "slice/overhangs/tree-supports                                             FAILED\n"
              "  threw std::bad_alloc in posSupportMaterial\n"
              "\n"
              "~ CV above the 3% significance bar, so a change that size there is noise\n"
              "\n"
              "1 ran, 1 skipped, 1 failed   6.1s\n");
}

TEST_CASE("under --verbose each scope has its own row and nothing folds", "[OrcaBench][Reporters]")
{
    ReportOptions options;
    options.verbose        = true;
    const std::string text = render("console", canned(), options);
    const std::string rows = "  psGCodeExport print             805.0   800.0   0.9%   87.9%    5.0x       420\n"
                             "  posInfill object:0               93.0    90.0   4.6%~  10.2%    9.0x       410\n"
                             "  posSlice object:0                12.5    12.0   5.7%~   1.4%       -         -\n"
                             "  posSimplifyInfill object:0        5.5     5.0  12.9%~   0.6%       -         -\n"
                             "  posContouring object:0        instant\n"
                             "  posEstimateCurledExtrusions object:0 never finished\n"
                             "  posSimplifySupportPath object:0  not run\n"
                             "  ---";
    CHECK(text.find(rows) != std::string::npos);
    CHECK(text.find("other (") == std::string::npos);
}

TEST_CASE("rows sort by mean, or by first start for pipeline order", "[OrcaBench][Reporters]")
{
    ReportOptions options;
    options.sort_by        = SortBy::Start;
    const std::string text = render("console", canned(), options);
    const auto        at   = [&text](const std::string& stage) { return text.find("  " + stage + " "); };
    CHECK(at("posSlice") < at("posInfill"));
    CHECK(at("posInfill") < at("posEstimateCurledExtrusions"));
    CHECK(at("posEstimateCurledExtrusions") < at("psGCodeExport"));
    CHECK(at("psGCodeExport") < text.find("  other ("));
}

TEST_CASE("the console marks shared readings and CPU the Windows floor leaves out, with a legend for each", "[OrcaBench][Reporters]")
{
    Result result     = canned();
    result.machine.os = "Windows 10.0.26200";
    for (IterationResult& iteration : result.workloads.front().iterations) {
        StageSpan& infill = iteration.timeline[1];
        infill.done_at -= 10ms;
        infill.started_at -= 10ms;
    }
    const std::string text = render("console", result);
    CHECK(text.find("  posInfill*                       93.0    90.0   4.6%~  10.2%       -       410\n") != std::string::npos);
    CHECK(text.find("  posSlice*                        12.5    12.0   5.7%~   1.4%       -         -\n") != std::string::npos);
    CHECK(text.find("  psGCodeExport                   805.0   800.0   0.9%   87.9%    5.0x       420\n") != std::string::npos);
    CHECK(text.find("\n* readings shared with stages that ran at the same time\n") != std::string::npos);
    CHECK(text.find("\nCPU left out where Windows' 15.6 ms steps could skew it past 10%\n") != std::string::npos);
}

TEST_CASE("the header marks a build whose flags turn optimization off", "[OrcaBench][Reporters]")
{
    const auto [flags, marked] = GENERATE(table<std::string, bool>({
        {"/Zi /Od /Ob0 /DNDEBUG", true},
        {"-g -O0", true},
        {"/O2 /Ob2 /DNDEBUG", false},
        {"-O3 -march=native", false},
    }));
    CAPTURE(flags);
    Result result = canned();
    result.build.config = "RelWithDebInfo";
    result.build.flags  = flags;
    CHECK((render("console", result).find("  RelWithDebInfo (unoptimized)  ") != std::string::npos) == marked);
}

TEST_CASE("a workload with no timed passes shows only what it recorded", "[OrcaBench][Reporters]")
{
    Result result    = canned();
    result.workloads = {WorkloadResult::ran("slice/extruder-idler/standard-0.20", 0x3bbecdbaa64c8fe8, std::nullopt, {}),
                        WorkloadResult::ran("slice/benchy/standard-0.20", std::nullopt, std::nullopt, {})};
    CHECK(render("console", result) == std::string(canned_header) +
                                           "\n"
                                           "slice/extruder-idler/standard-0.20\n"
                                           "  hash 3bbecdba\n"
                                           "\n"
                                           "slice/benchy/standard-0.20\n"
                                           "\n"
                                           "2 ran, 0 skipped, 0 failed   6.1s\n");
}

TEST_CASE("a workload whose passes took no time shows no unaccounted share", "[OrcaBench][Reporters]")
{
    Result result    = canned();
    result.workloads = {WorkloadResult::ran("slice/idler", std::nullopt, std::nullopt, {IterationResult {}})};
    const std::string text = render("console", result);
    const std::size_t at   = text.find("  unaccounted");
    REQUIRE(at != std::string::npos);
    CHECK(text.substr(at, text.find('\n', at) - at) == "  unaccounted" + std::string(23, ' ') + "0.0");
}

TEST_CASE("json writes the whole document once the run is done, whatever the console would fold", "[OrcaBench][Reporters]")
{
    ReportOptions options;
    options.collapse_below = 0.5;
    std::ostringstream              out;
    const std::unique_ptr<Reporter> json   = make_reporter("json", out, options);
    const Result                    result = canned();
    Result                          header = result;
    header.workloads.clear();
    json->started(header);
    for (const WorkloadResult& workload : result.workloads)
        json->workload(workload);
    CHECK(out.str().empty());
    json->finished(result);
    CHECK(out.str() == write_document(result));
}

TEST_CASE("null writes nothing", "[OrcaBench][Reporters]")
{
    CHECK(render("null", canned()).empty());
}

TEST_CASE("the reporters are console, json and null, and any other name is refused", "[OrcaBench][Reporters]")
{
    CHECK(reporter_names() == std::vector<std::string> {"console", "json", "null"});
    std::ostringstream out;
    CHECK_THROWS_AS(make_reporter("xml", out, {}), std::invalid_argument);
}

TEST_CASE("in a terminal the progress line is redrawn after each pass and erased on request", "[OrcaBench][Reporters]")
{
    std::ostringstream out;
    Clock::time_point  now = Clock::time_point {} + 1h;
    Progress           progress(out, true, [&now]() { return now; });
    progress.workload_started(2, 20, "slice/benchy/standard-0.20");
    // Each pass takes 1.5 s, so the time left is the passes left at that pace.
    const std::uint64_t   passes  = 4;
    const Clock::duration walls[] = {1210ms, 1190ms, 1200ms, 1200ms};
    for (std::uint64_t pass = 1; pass <= passes; ++pass) {
        now += 1500ms;
        progress.pass_done({pass, 1, passes, walls[pass - 1]});
    }
    progress.clear();

    const std::string name   = "[2/20] slice/benchy/standard-0.20";
    const std::string warmup = name + "  warmup 1/1  last 1.21 s  about 5 s left";
    const std::string first  = name + "  timed 1/3  last 1.19 s  about 3 s left";
    const std::string second = name + "  timed 2/3  last 1.20 s  about 2 s left";
    const std::string third  = name + "  timed 3/3  last 1.20 s";
    CHECK(out.str() == "\r" + name + "\r" + warmup + "\r" + first + " \r" + second + "\r" + third +
                           std::string(second.size() - third.size(), ' ') + "\r" + std::string(third.size(), ' ') + "\r");
}

TEST_CASE("in a log the progress line is printed once per workload", "[OrcaBench][Reporters]")
{
    std::ostringstream out;
    Progress           progress(out, false);
    progress.workload_started(1, 2, "slice/idler");
    progress.pass_done({1, 1, 2, 1s});
    progress.clear();
    progress.workload_started(2, 2, "slice/benchy");
    CHECK(out.str() == "[1/2] slice/idler\n[2/2] slice/benchy\n");
}

TEST_CASE("the report's events erase the progress line before a workload prints", "[OrcaBench][Reporters]")
{
    std::ostringstream              out;
    Progress                        progress(out, true);
    const std::unique_ptr<Reporter> console = make_reporter("console", out, {});
    const RunEvents                 events  = report_events(*console, &progress);
    const Result                    result  = canned();
    Result                          header  = result;
    header.workloads.clear();

    events.started(header);
    events.workload_started(2, 3, result.workloads[1].name);
    events.workload_done(result.workloads[1]);

    const std::string line = "[2/3] slice/benchy/standard-0.20";
    CHECK(out.str() == canned_header + ("\r" + line + "\r" + std::string(line.size(), ' ') + "\r") + skipped_block);
}

TEST_CASE("without a progress line only the reporter hears the run", "[OrcaBench][Reporters]")
{
    std::ostringstream              out;
    const std::unique_ptr<Reporter> console = make_reporter("console", out, {});
    const RunEvents                 events  = report_events(*console, nullptr);
    CHECK(static_cast<bool>(events.started));
    CHECK(static_cast<bool>(events.workload_done));
    CHECK_FALSE(static_cast<bool>(events.workload_started));
    CHECK_FALSE(static_cast<bool>(events.pass_done));
}

TEST_CASE("a run reported as it goes prints what its finished result prints", "[OrcaBench][Reporters]")
{
    FakeHooks hooks;
    hooks.execute = [](unsigned, Measurement& measurement) {
        const Clock::time_point at = Clock::now();
        measurement.span("posSlice", Scope::object(0), at, at);
    };
    WorkloadKinds kinds;
    kinds.add("fake", [hooks](const CatalogEntry& entry) { return std::make_unique<FakeWorkload>(entry, hooks); });
    std::vector<CatalogEntry> entries(2);
    entries[0].name = "fake/cube";
    entries[0].kind = "fake";
    entries[1].name = "fake/cylinder";
    entries[1].kind = "fake";
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);

    std::ostringstream              live;
    const std::unique_ptr<Reporter> console = make_reporter("console", live, {});
    const RunEvents                 events  = report_events(*console, nullptr);
    const Result                    result  = run_suite(entries, Policy::resolve("quick", {}, 1), kinds, environment, host_reading, events);
    console->finished(result);
    CHECK(live.str() == render("console", result));
}
