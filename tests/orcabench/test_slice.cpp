#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/Policy.hpp"
#include "core/Result.hpp"
#include "core/Runner.hpp"
#include "core/Workload.hpp"
#include "slicer/Environment.hpp"
#include "slicer/Output.hpp"

#include "test_utils.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/filesystem/path.hpp>

#include <cstdint>
#include <fstream>
#include <ios>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Bench;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinRel;

namespace {

Policy timing(StageSet stages, unsigned iterations)
{
    PolicyOverrides overrides;
    overrides.warmup     = 0;
    overrides.iterations = iterations;
    overrides.stages     = std::move(stages);
    return Policy::resolve("quick", overrides, 2);
}

CatalogEntry smoke_cube(Properties config = {})
{
    CatalogEntry entry;
    entry.name    = "slice/smoke-cube/test";
    entry.kind    = "slice";
    entry.fixture = "procedural:smoke-cube";
    entry.config  = std::move(config);
    return entry;
}

WorkloadResult slice(const CatalogEntry& entry, const Policy& policy)
{
    SlicerEnvironment environment(ORCABENCH_RESOURCES_DIR);
    Result            result = run_suite({entry}, policy, WorkloadKinds::instance(), environment);
    REQUIRE(result.workloads.size() == 1);
    return std::move(result.workloads.front());
}

using Reports = std::multiset<std::pair<std::string, std::string>>;

// Every stage and scope the pass reported, in whichever of the three forms.
Reports reported(const IterationResult& pass)
{
    Reports reports;
    for (const StageSpan& span : pass.timeline)
        reports.emplace(span.stage, span.scope);
    for (const UnfinishedStage& stage : pass.unfinished)
        reports.emplace(stage.stage, stage.scope);
    for (const StageNotRun& stage : pass.not_run)
        reports.emplace(stage.stage, stage.scope);
    return reports;
}

const Reports object_steps = {{"posSlice", "object:0"},
                              {"posPerimeters", "object:0"},
                              {"posEstimateCurledExtrusions", "object:0"},
                              {"posPrepareInfill", "object:0"},
                              {"posInfill", "object:0"},
                              {"posIroning", "object:0"},
                              {"posContouring", "object:0"},
                              {"posSupportMaterial", "object:0"},
                              {"posSimplifyPath", "object:0"},
                              {"posSimplifySupportPath", "object:0"},
                              {"posDetectOverhangsForLift", "object:0"},
                              {"posSimplifyWall", "object:0"},
                              {"posSimplifyInfill", "object:0"}};

void write(const boost::filesystem::path& path, const std::string& text) { std::ofstream(path.string(), std::ios::binary) << text; }

} // namespace

TEST_CASE("a slice reports each step of the stages it times in one form, the step upstream never finishes as unfinished",
          "[OrcaBench][Slice]")
{
    const WorkloadResult sliced = slice(smoke_cube(), timing({Stage::Process, Stage::Export}, 1));
    CAPTURE(sliced.reason);
    REQUIRE(sliced.outcome == Outcome::Ran);
    REQUIRE(sliced.iterations.size() == 1);
    const IterationResult& pass     = sliced.iterations.front();
    Reports                expected = object_steps;
    expected.insert({{"psWipeTower", "print"}, {"psSkirtBrim", "print"}, {"psGCodeExport", "print"}});
    CHECK(reported(pass) == expected);
    REQUIRE(pass.unfinished.size() == 1);
    CHECK(pass.unfinished.front().stage == "posEstimateCurledExtrusions");
}

TEST_CASE("every pass of a slice writes the same G-code, whose work matches what was set", "[OrcaBench][Slice]")
{
    const WorkloadResult sliced = slice(smoke_cube({{"layer_height", "0.25"}, {"initial_layer_print_height", "0.25"}}),
                                        timing({Stage::Process, Stage::Export}, 3));
    CAPTURE(sliced.reason);
    REQUIRE(sliced.outcome == Outcome::Ran);
    CHECK(sliced.output_hash.has_value());
    REQUIRE(sliced.work.has_value());
    const WorkStats& work = *sliced.work;
    // A 20 mm cube at 0.25 mm layers.
    CHECK(work.layers == 80);
    CHECK(work.travel_moves > 0);
    CHECK(work.gcode_bytes > 0);
    double        role_volume = 0.;
    std::uint64_t role_moves  = 0;
    for (const auto& [key, value] : work.metrics) {
        if (key.rfind("extrusion_mm3.", 0) == 0)
            role_volume += value;
        else if (key.rfind("moves.", 0) == 0)
            role_moves += std::uint64_t(value);
    }
    CHECK(role_moves == work.moves - work.travel_moves);
    CHECK_THAT(role_volume, WithinRel(work.extrusion_mm3, 1e-9));
}

TEST_CASE("a slice times only the stages the run asks for", "[OrcaBench][Slice]")
{
    SECTION("export alone runs process() untimed in prepare(), writing the G-code a whole slice writes")
    {
        const WorkloadResult sliced = slice(smoke_cube(), timing({Stage::Export}, 1));
        CAPTURE(sliced.reason);
        REQUIRE(sliced.outcome == Outcome::Ran);
        REQUIRE(sliced.iterations.size() == 1);
        CHECK(reported(sliced.iterations.front()) == Reports {{"psGCodeExport", "print"}});
        REQUIRE(sliced.output_hash.has_value());
        CHECK(sliced.output_hash == slice(smoke_cube(), timing({Stage::Process, Stage::Export}, 1)).output_hash);
    }
    SECTION("process alone writes no G-code")
    {
        const WorkloadResult sliced = slice(smoke_cube(), timing({Stage::Process}, 1));
        CAPTURE(sliced.reason);
        REQUIRE(sliced.outcome == Outcome::Ran);
        REQUIRE(sliced.iterations.size() == 1);
        Reports expected = object_steps;
        expected.insert({{"psWipeTower", "print"}, {"psSkirtBrim", "print"}});
        CHECK(reported(sliced.iterations.front()) == expected);
        CHECK_FALSE(sliced.output_hash.has_value());
    }
}

TEST_CASE("a slice whose fixture is missing is skipped with the path it looked for", "[OrcaBench][Slice]")
{
    CatalogEntry entry          = smoke_cube();
    entry.fixture               = "handy:missing.drc";
    const WorkloadResult sliced = slice(entry, timing({Stage::Process, Stage::Export}, 1));
    CHECK(sliced.outcome == Outcome::Skipped);
    CHECK_THAT(sliced.reason, StartsWith("fixture not available: ") && EndsWith("missing.drc is missing"));
}

TEST_CASE("a slice whose config holds a key the slicer does not know fails, naming the key", "[OrcaBench][Slice]")
{
    const WorkloadResult sliced = slice(smoke_cube({{"no_such_key", "1"}}), timing({Stage::Process, Stage::Export}, 1));
    CHECK(sliced.outcome == Outcome::Failed);
    CHECK_THAT(sliced.reason, StartsWith("setup() threw: ") && ContainsSubstring("no_such_key"));
}

TEST_CASE("a slice whose config fails validation fails, giving validate()'s reason", "[OrcaBench][Slice]")
{
    const WorkloadResult sliced = slice(smoke_cube({{"layer_change_gcode", ""}}), timing({Stage::Process, Stage::Export}, 1));
    CHECK(sliced.outcome == Outcome::Failed);
    CHECK_THAT(sliced.reason, StartsWith("prepare() threw") && ContainsSubstring("validate(): ") && ContainsSubstring("G92 E0"));
}

TEST_CASE("the G-code hash leaves out the header's generation time and nothing else", "[OrcaBench][Slice]")
{
    const ScopedTemporaryFile first(".gcode");
    const ScopedTemporaryFile second(".gcode");
    const ScopedTemporaryFile moved(".gcode");
    write(first.path(), "; HEADER_BLOCK_START\n; generated by OrcaSlicer 2.3.1 on 2026-10-01 at 10:00:00\nG1 X1 Y1\n");
    write(second.path(), "; HEADER_BLOCK_START\n; generated by OrcaSlicer 2.3.1 on 2026-10-01 at 10:00:59\nG1 X1 Y1\n");
    write(moved.path(), "; HEADER_BLOCK_START\n; generated by OrcaSlicer 2.3.1 on 2026-10-01 at 10:00:00\nG1 X1 Y2\n");
    CHECK(gcode_hash(first.string()) == gcode_hash(second.string()));
    CHECK(gcode_hash(first.string()) != gcode_hash(moved.string()));
    CHECK_THROWS_WITH(gcode_hash(first.string() + ".missing"), "cannot read " + first.string() + ".missing");
}

TEST_CASE("the work stats count the processor's moves by kind, layer and extrusion role", "[OrcaBench][Slice]")
{
    GCodeProcessorResult result;
    result.filament_diameters = {2.};
    const auto add_move       = [&result](EMoveType type, ExtrusionRole role, unsigned int layer, float filament) {
        GCodeProcessorResult::MoveVertex vertex;
        vertex.type           = type;
        vertex.extrusion_role = role;
        vertex.layer_id       = layer;
        vertex.delta_extruder = filament;
        result.moves.push_back(vertex);
    };
    add_move(EMoveType::Travel, erNone, 2, 0.f);
    add_move(EMoveType::Extrude, erPerimeter, 0, 1.f);
    add_move(EMoveType::Extrude, erPerimeter, 0, 1.f);
    add_move(EMoveType::Retract, erNone, 0, -0.8f);
    add_move(EMoveType::Extrude, erSupportMaterial, 1, 2.f);

    const WorkStats work = work_of(result, 4096);
    CHECK(work.moves == 4);
    CHECK(work.travel_moves == 1);
    CHECK(work.layers == 2);
    CHECK(work.gcode_bytes == 4096);
    // 2 mm filament gives PI mm3 per mm.
    CHECK_THAT(work.extrusion_mm3, WithinRel(4 * PI, 1e-6));
    CHECK(work.metrics.at("moves.perimeter") == 2.);
    CHECK(work.metrics.at("moves.support") == 1.);
    CHECK_THAT(work.metrics.at("extrusion_mm3.perimeter"), WithinRel(2 * PI, 1e-6));
    CHECK_THAT(work.metrics.at("extrusion_mm3.support"), WithinRel(2 * PI, 1e-6));
    CHECK(work.metrics.size() == 4);
}
