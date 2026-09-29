#include <catch2/catch_all.hpp>

#include "core/BuildId.hpp"
#include "core/Host.hpp"
#include "core/Runner.hpp"
#include "orcabench_test_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Slic3r::Bench;
using namespace Slic3r::Bench::Test;

namespace {

CatalogEntry entry_of(std::string name, std::string kind = "fake")
{
    CatalogEntry entry;
    entry.name = std::move(name);
    entry.kind = std::move(kind);
    return entry;
}

void add_fake(WorkloadKinds& kinds, const std::string& kind, const FakeHooks& hooks, std::vector<std::string>* calls = nullptr)
{
    kinds.add(kind, [hooks, calls](const CatalogEntry& entry) { return std::make_unique<FakeWorkload>(entry, hooks, calls); });
}

// A registry whose "fake" kind builds a FakeWorkload with these hooks, logging its calls.
WorkloadKinds fake_kinds(const FakeHooks& hooks, std::vector<std::string>& calls)
{
    WorkloadKinds kinds;
    add_fake(kinds, "fake", hooks, &calls);
    return kinds;
}

// One warmup and three timed passes, collecting everything.
Policy quick() { return Policy::resolve("quick", {}, 1); }

// Reports a span at least one clock tick long, the pass as a metric, and the same output on every
// pass.
void report(unsigned pass, Measurement& measurement)
{
    const Clock::time_point started_at = Clock::now();
    while (Clock::now() == started_at) {}
    measurement.span("posSlice", Scope::print(), started_at, Clock::now());
    measurement.metric("pass", pass);
    WorkStats work;
    work.layers = 190;
    measurement.work(work);
    measurement.output_hash(0x3bbecdbaa64c8fe8);
}

std::vector<std::string> names_of(const Result& result)
{
    std::vector<std::string> names;
    for (const WorkloadResult& workload : result.workloads)
        names.push_back(workload.name);
    return names;
}

} // namespace

TEST_CASE("setup runs once, then prepare and execute run for every warmup and timed pass", "[OrcaBench][Runner]")
{
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    run_suite({entry_of("fake/cube")}, quick(), fake_kinds({}, calls), environment);
    CHECK(calls == std::vector<std::string> {"enter", "setup", "prepare", "execute", "prepare", "execute", "prepare",
                                             "execute", "prepare", "execute", "leave"});
}

TEST_CASE("each timed pass is recorded with its spans and metrics, and a warmup pass is not", "[OrcaBench][Runner]")
{
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    FakeHooks                hooks;
    hooks.execute       = report;
    const Result result = run_suite({entry_of("fake/cube")}, quick(), fake_kinds(hooks, calls), environment);

    REQUIRE(result.workloads.size() == 1);
    const WorkloadResult& workload = result.workloads.front();
    REQUIRE(workload.outcome == Outcome::Ran);
    CHECK(workload.output_hash == std::optional<std::uint64_t>(0x3bbecdbaa64c8fe8));
    REQUIRE(workload.work.has_value());
    CHECK(workload.work->layers == 190);
    REQUIRE(workload.iterations.size() == 3);
    Clock::duration timed {};
    for (std::size_t i = 0; i < workload.iterations.size(); ++i) {
        const IterationResult& iteration = workload.iterations[i];
        CHECK_THAT(iteration.metrics.at("pass"), Catch::Matchers::WithinAbs(i + 2.0, 0.0));
        REQUIRE(iteration.timeline.size() == 1);
        const StageSpan& span = iteration.timeline.front();
        CHECK(span.stage == "posSlice");
        CHECK(iteration.wall > Clock::duration::zero());
        CHECK(iteration.wall >= span.done_at - span.started_at);
        timed += iteration.wall;
    }
    CHECK(result.suite.duration >= timed);
}

TEST_CASE("a workload is told which stages to time", "[OrcaBench][Runner]")
{
    PolicyOverrides overrides;
    overrides.stages = StageSet {Stage::Export};

    StageSet  timed;
    FakeHooks hooks;
    hooks.setup = [&timed](const RunContext& context) {
        timed = context.timed;
        return std::optional<std::string>();
    };
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    run_suite({entry_of("fake/cube")}, Policy::resolve("quick", overrides, 1), fake_kinds(hooks, calls), environment);
    CHECK(timed == StageSet {Stage::Export});
}

TEST_CASE("the environment is entered once before the first workload and left after the last", "[OrcaBench][Runner]")
{
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    const Policy             policy = quick();
    run_suite({entry_of("fake/cube"), entry_of("fake/cylinder")}, policy, fake_kinds({}, calls), environment);
    REQUIRE(calls.size() > 2);
    CHECK(calls.front() == "enter");
    CHECK(calls.back() == "leave");
    CHECK(std::count(calls.begin(), calls.end(), "enter") == 1);
    CHECK(std::count(calls.begin(), calls.end(), "leave") == 1);
    CHECK(std::count(calls.begin(), calls.end(), "setup") == 2);
    CHECK(environment.threads == policy.threads);
}

TEST_CASE("an environment that fails to enter ends the run before any workload", "[OrcaBench][Runner]")
{
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls, true);
    CHECK_THROWS_WITH(run_suite({entry_of("fake/cube")}, quick(), fake_kinds({}, calls), environment),
                      "the environment refused to enter");
    CHECK(calls == std::vector<std::string> {"enter"});
}

TEST_CASE("a skipped workload keeps its reason and is never prepared", "[OrcaBench][Runner]")
{
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    FakeHooks                hooks;
    hooks.setup         = [](const RunContext&) { return std::optional<std::string>("fixture not available"); };
    const Result result = run_suite({entry_of("fake/cube")}, quick(), fake_kinds(hooks, calls), environment);
    REQUIRE(result.workloads.size() == 1);
    CHECK(result.workloads.front().outcome == Outcome::Skipped);
    CHECK(result.workloads.front().reason == "fixture not available");
    CHECK(std::count(calls.begin(), calls.end(), "prepare") == 0);
}

TEST_CASE("an empty reason to skip fails the workload", "[OrcaBench][Runner]")
{
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    FakeHooks                hooks;
    hooks.setup         = [](const RunContext&) { return std::optional<std::string>(""); };
    const Result result = run_suite({entry_of("fake/cube")}, quick(), fake_kinds(hooks, calls), environment);
    REQUIRE(result.workloads.size() == 1);
    CHECK(result.workloads.front().outcome == Outcome::Failed);
    CHECK(result.workloads.front().reason == "setup() gave an empty reason to skip");
}

TEST_CASE("a throw fails only its own workload, naming the call and the pass", "[OrcaBench][Runner]")
{
    // quick's one warmup pass comes first, so the fake's pass 3 is timed pass 2.
    const auto [kind, reason] = GENERATE(table<std::string, std::string>({
        {"missing", "create() threw: fake/cube is of the kind 'missing', which no workload registered"},
        {"throws-in-setup", "setup() threw: no fixture"},
        {"throws-in-prepare", "prepare() threw on warmup pass 1: out of memory"},
        {"throws-in-execute", "execute() threw on timed pass 2: out of memory"},
        {"throws-an-int", "execute() threw on timed pass 2: something that is not a std::exception"},
    }));
    CAPTURE(kind);
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    WorkloadKinds            kinds = fake_kinds({}, calls);

    FakeHooks in_setup;
    in_setup.setup = [](const RunContext&) -> std::optional<std::string> { throw std::runtime_error("no fixture"); };
    add_fake(kinds, "throws-in-setup", in_setup);
    FakeHooks in_prepare;
    in_prepare.prepare = [](unsigned pass) {
        if (pass == 1)
            throw std::runtime_error("out of memory");
    };
    add_fake(kinds, "throws-in-prepare", in_prepare);
    FakeHooks in_execute;
    in_execute.execute = [](unsigned pass, Measurement&) {
        if (pass == 3)
            throw std::runtime_error("out of memory");
    };
    add_fake(kinds, "throws-in-execute", in_execute);
    FakeHooks an_int;
    an_int.execute = [](unsigned pass, Measurement&) {
        if (pass == 3)
            throw 42;
    };
    add_fake(kinds, "throws-an-int", an_int);

    const Result result = run_suite({entry_of("fake/cube", kind), entry_of("fake/cylinder")}, quick(), kinds, environment);
    REQUIRE(result.workloads.size() == 2);
    CHECK(result.workloads[0].outcome == Outcome::Failed);
    CHECK(result.workloads[0].reason == reason);
    CHECK(result.workloads[1].outcome == Outcome::Ran);
}

TEST_CASE("a workload that reports its first pass's span again is failed", "[OrcaBench][Runner]")
{
    Clock::time_point first_start;
    FakeHooks         hooks;
    hooks.execute = [&first_start](unsigned pass, Measurement& measurement) {
        if (pass == 1) {
            first_start = Clock::now();
            // Waits for the clock to tick, so the next pass starts after this span.
            while (Clock::now() == first_start) {}
        }
        measurement.span("posSlice", Scope::print(), first_start, first_start);
    };
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    const Result             result = run_suite({entry_of("fake/cube")}, quick(), fake_kinds(hooks, calls), environment);
    REQUIRE(result.workloads.size() == 1);
    CHECK(result.workloads.front().outcome == Outcome::Failed);
    CHECK(result.workloads.front().reason == "execute() threw on timed pass 1: the posSlice span starts before its iteration");
}

TEST_CASE("a pass whose output differs from the first pass fails the workload", "[OrcaBench][Runner]")
{
    const auto [differs, reason] = GENERATE(table<std::string, std::string>({
        {"hash", "the output hash of timed pass 3 differs from warmup pass 1"},
        {"work", "the work stats of timed pass 1 differ from warmup pass 1"},
    }));
    CAPTURE(differs);
    FakeHooks hooks;
    hooks.execute = [differs = differs](unsigned pass, Measurement& measurement) {
        WorkStats work;
        work.layers = (differs == "work" && pass == 2) ? 191 : 190;
        measurement.work(work);
        measurement.output_hash((differs == "hash" && pass == 4) ? 2 : 1);
    };
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    const Result             result = run_suite({entry_of("fake/cube")}, quick(), fake_kinds(hooks, calls), environment);
    REQUIRE(result.workloads.size() == 1);
    CHECK(result.workloads.front().outcome == Outcome::Failed);
    CHECK(result.workloads.front().reason == reason);
}

TEST_CASE("what the policy does not collect is absent from the result", "[OrcaBench][Runner]")
{
    // The hash changes on every pass, which fails a workload only where the policy collects it.
    FakeHooks hooks;
    hooks.execute = [](unsigned pass, Measurement& measurement) {
        const Clock::time_point started_at = Clock::now();
        measurement.span("posSlice", Scope::print(), started_at, Clock::now());
        WorkStats work;
        work.layers = 190;
        measurement.work(work);
        measurement.output_hash(pass);
    };
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    const WorkloadKinds      kinds = fake_kinds(hooks, calls);

    SECTION("verify keeps the hash of its one pass and records no iteration")
    {
        const Result result = run_suite({entry_of("fake/cube")}, Policy::resolve("verify", {}, 1), kinds, environment);
        REQUIRE(result.workloads.size() == 1);
        const WorkloadResult& workload = result.workloads.front();
        REQUIRE(workload.outcome == Outcome::Ran);
        CHECK(workload.output_hash == std::optional<std::uint64_t>(1));
        CHECK_FALSE(workload.work.has_value());
        CHECK(workload.iterations.empty());
    }
    SECTION("pgo keeps nothing")
    {
        const Result result = run_suite({entry_of("fake/cube")}, Policy::resolve("pgo", {}, 1), kinds, environment);
        REQUIRE(result.workloads.size() == 1);
        const WorkloadResult& workload = result.workloads.front();
        REQUIRE(workload.outcome == Outcome::Ran);
        CHECK_FALSE(workload.output_hash.has_value());
        CHECK_FALSE(workload.work.has_value());
        CHECK(workload.iterations.empty());
    }
    SECTION("quick without export drops the hash, so a changing hash does not fail it")
    {
        PolicyOverrides overrides;
        overrides.stages    = StageSet {Stage::Process};
        const Result result = run_suite({entry_of("fake/cube")}, Policy::resolve("quick", overrides, 1), kinds, environment);
        REQUIRE(result.workloads.size() == 1);
        const WorkloadResult& workload = result.workloads.front();
        REQUIRE(workload.outcome == Outcome::Ran);
        CHECK_FALSE(workload.output_hash.has_value());
        REQUIRE(workload.work.has_value());
        CHECK(workload.work->layers == 190);
        CHECK(workload.iterations.size() == 3);
    }
}

TEST_CASE("under pgo an entry that may not train is left out of the run", "[OrcaBench][Runner]")
{
    CatalogEntry micro = entry_of("fake/micro");
    micro.tier         = Tier::Micro;

    CatalogEntry opted_in = entry_of("fake/micro-opted-in");
    opted_in.tier         = Tier::Micro;
    opted_in.pgo_eligible = true;

    const std::vector<CatalogEntry> entries {micro, entry_of("fake/macro"), opted_in};
    std::vector<std::string>        calls;
    FakeEnvironment                 environment(calls);
    const WorkloadKinds             kinds = fake_kinds({}, calls);
    CHECK(names_of(run_suite(entries, Policy::resolve("pgo", {}, 1), kinds, environment)) ==
          std::vector<std::string> {"fake/macro", "fake/micro-opted-in"});
    CHECK(names_of(run_suite(entries, quick(), kinds, environment)) ==
          std::vector<std::string> {"fake/micro", "fake/macro", "fake/micro-opted-in"});
}

TEST_CASE("the result records the policy's identity, this build and this machine", "[OrcaBench][Runner]")
{
    std::vector<std::string> calls;
    FakeEnvironment          environment(calls);
    const Policy             policy  = quick();
    const auto               before  = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const Clock::time_point  started = Clock::now();
    const Result             result  = run_suite({}, policy, fake_kinds({}, calls), environment);
    const Clock::duration    elapsed = Clock::now() - started;
    const auto               after   = std::chrono::system_clock::now();

    CHECK(result.measurement == policy.identity());
    CHECK(result.build.revision == build_revision);
    CHECK(result.build.dirty == build_dirty);
    CHECK(result.build.compiler == build_compiler);
    CHECK(result.build.compiler_version == build_compiler_version);
    CHECK(result.build.config == build_config);
    CHECK(result.build.flags == build_flags);
    CHECK(result.machine.host == host_name());
    CHECK(result.machine.os == os_description());
    CHECK(result.machine.cpu == cpu_model());
    CHECK(result.machine.logical_cores == logical_cores());
    CHECK(result.suite.started_at >= before);
    CHECK(result.suite.started_at <= after);
    CHECK(result.suite.duration <= elapsed);
    CHECK(result.workloads.empty());
}
