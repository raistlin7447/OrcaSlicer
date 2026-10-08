#include <catch2/catch_all.hpp>

#include "core/Catalog.hpp"
#include "core/Policy.hpp"
#include "core/Runner.hpp"
#include "slicer/Environment.hpp"
#include "slicer/Fixtures.hpp"

#include "libslic3r/Utils.hpp"

#include <boost/filesystem/operations.hpp>

#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Bench;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;

namespace {

Policy timing(StageSet stages, unsigned iterations)
{
    PolicyOverrides overrides;
    overrides.warmup     = 0;
    overrides.iterations = iterations;
    overrides.stages     = std::move(stages);
    return Policy::resolve("quick", overrides, 2);
}

CatalogEntry loading(std::string fixture, Properties config = {})
{
    CatalogEntry entry;
    entry.name    = "load/test/file";
    entry.kind    = "load";
    entry.fixture = std::move(fixture);
    entry.config  = std::move(config);
    return entry;
}

WorkloadResult load(const CatalogEntry& entry, const Policy& policy)
{
    SlicerEnvironment environment(ORCABENCH_RESOURCES_DIR);
    Result            result = run_suite({entry}, policy, WorkloadKinds::instance(), environment);
    REQUIRE(result.workloads.size() == 1);
    return std::move(result.workloads.front());
}

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

std::vector<CatalogEntry> loading_catalog()
{
    std::ifstream file(std::string(ORCABENCH_CATALOG_DIR) + "/loading.json", std::ios::binary);
    REQUIRE(file);
    return read_catalog(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}

} // namespace

TEST_CASE("a load times reading the file as its one step, with the facets, objects and parts it read", "[OrcaBench][Load]")
{
    const WorkloadResult loaded = load(loading("handy:OrcaBadge.3mf"), timing({Stage::Load}, 1));
    CAPTURE(loaded.reason);
    REQUIRE(loaded.outcome == Outcome::Ran);
    REQUIRE(loaded.iterations.size() == 1);
    const IterationResult& pass = loaded.iterations.front();
    REQUIRE(pass.timeline.size() == 1);
    const StageSpan& span = pass.timeline.front();
    CHECK(span.stage == "read_from_file");
    CHECK(span.scope == "print");
    CHECK(span.metrics.at("facets") == 72586.);
    CHECK(span.metrics.at("objects") == 3.);
    CHECK(span.metrics.at("volumes") == 39.);
    CHECK(pass.unfinished.empty());
    CHECK(pass.not_run.empty());
    CHECK(loaded.output_hash.has_value());
}

TEST_CASE("every pass of a load reads the same geometry", "[OrcaBench][Load]")
{
    const WorkloadResult loaded = load(loading("handy:Stanford_Bunny.drc"), timing({Stage::Load}, 3));
    CAPTURE(loaded.reason);
    REQUIRE(loaded.outcome == Outcome::Ran);
    CHECK(loaded.iterations.size() == 3);
    CHECK(loaded.output_hash.has_value());
}

TEST_CASE("a load reads the fixture written as a binary or an ASCII STL when its format asks, the same geometry either way",
          "[OrcaBench][Load]")
{
    const WorkloadResult binary = load(loading("procedural:smoke-cube", {{"format", "stl"}}), timing({Stage::Load}, 1));
    const WorkloadResult ascii  = load(loading("procedural:smoke-cube", {{"format", "stl-ascii"}}), timing({Stage::Load}, 1));
    for (const WorkloadResult* loaded : {&binary, &ascii}) {
        CAPTURE(loaded->reason);
        REQUIRE(loaded->outcome == Outcome::Ran);
        REQUIRE(loaded->iterations.size() == 1);
        REQUIRE(loaded->iterations.front().timeline.size() == 1);
        CHECK(loaded->iterations.front().timeline.front().metrics.at("facets") == 12.);
        REQUIRE(loaded->output_hash.has_value());
    }
    CHECK(binary.output_hash == ascii.output_hash);
}

TEST_CASE("a load times nothing when the run does not time loading", "[OrcaBench][Load]")
{
    const WorkloadResult loaded = load(loading("handy:Stanford_Bunny.drc"), timing({Stage::Process, Stage::Export}, 1));
    CAPTURE(loaded.reason);
    REQUIRE(loaded.outcome == Outcome::Ran);
    REQUIRE(loaded.iterations.size() == 1);
    CHECK(loaded.iterations.front().timeline.empty());
    CHECK_FALSE(loaded.output_hash.has_value());
}

TEST_CASE("a load whose fixture is missing is skipped with the path it looked for", "[OrcaBench][Load]")
{
    for (const CatalogEntry& entry : {loading("handy:missing.drc"), loading("handy:missing.drc", {{"format", "stl"}})}) {
        const WorkloadResult loaded = load(entry, timing({Stage::Load}, 1));
        CAPTURE(entry.config);
        CHECK(loaded.outcome == Outcome::Skipped);
        CHECK_THAT(loaded.reason, StartsWith("fixture not available: ") && EndsWith("missing.drc is missing"));
    }
}

TEST_CASE("a load refuses a procedural fixture without a format, a format it does not know and any other setting", "[OrcaBench][Load]")
{
    const auto [entry, refusal] = GENERATE(table<CatalogEntry, std::string>({
        {loading("procedural:smoke-cube"), "procedural:smoke-cube has no file to read, so the entry needs a format"},
        {loading("handy:3DBenchy.drc", {{"format", "obj"}}), "a load's format is stl or stl-ascii, not 'obj'"},
        {loading("handy:3DBenchy.drc", {{"layer_height", "0.2"}}), "a load has no setting called 'layer_height'"},
    }));
    CAPTURE(entry.fixture);
    const WorkloadResult loaded = load(entry, timing({Stage::Load}, 1));
    CHECK(loaded.outcome == Outcome::Failed);
    CHECK_THAT(loaded.reason, StartsWith("setup() threw: ") && ContainsSubstring(refusal));
}

TEST_CASE("the loading catalog reads three formats of one mesh, two 3MF projects and a procedural STL", "[OrcaBench][Load]")
{
    const std::vector<CatalogEntry> catalog = loading_catalog();
    REQUIRE_FALSE(catalog.empty());
    std::set<std::string> names;
    for (const CatalogEntry& entry : catalog) {
        CAPTURE(entry.name);
        CHECK(entry.kind == "load");
        CHECK(std::regex_match(entry.name, std::regex("load/[a-z0-9-]+/(drc|3mf|stl|stl-ascii)")));
        names.insert(entry.name);
    }
    CHECK(names == std::set<std::string> {"load/3dbenchy/drc", "load/3dbenchy/stl", "load/3dbenchy/stl-ascii", "load/stanford-bunny/drc",
                                          "load/orcabadge/3mf", "load/orcasliced/3mf", "load/fine-sphere/stl"});
}

TEST_CASE("every file the loading catalog reads is there", "[OrcaBench][Load]")
{
    const TreeResources resources;
    for (const CatalogEntry& entry : loading_catalog()) {
        CAPTURE(entry.name);
        const std::optional<std::string> path = fixture_path(entry.fixture);
        if (entry.config.empty())
            REQUIRE(path);
        if (path)
            CHECK(boost::filesystem::exists(*path));
    }
}
