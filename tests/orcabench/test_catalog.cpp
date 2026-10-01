#include <catch2/catch_all.hpp>

#include "core/Catalog.hpp"

#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r::Bench;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

namespace {

std::string catalogs(const char* name) { return std::string(TEST_DATA_DIR) + "/orcabench/catalogs/" + name; }

std::vector<CatalogEntry> slicing_catalog()
{
    std::ifstream file(std::string(ORCABENCH_CATALOG_DIR) + "/slicing.json", std::ios::binary);
    REQUIRE(file);
    return read_catalog(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}

} // namespace

TEST_CASE("a catalog entry reads every field, and one it leaves out takes its default", "[OrcaBench][Catalog]")
{
    const std::vector<CatalogEntry> catalog = read_catalog(R"({"entries": [
        {"name": "slice/cube/fine", "kind": "slice", "fixture": "procedural:cube", "tier": "micro", "pgo_eligible": true,
         "tags": ["smoke", "procedural"], "config": {"layer_height": "0.1"}},
        {"name": "slice/cube/plain", "kind": "slice"}]})");
    REQUIRE(catalog.size() == 2);
    const CatalogEntry& full = catalog[0];
    CHECK(full.name == "slice/cube/fine");
    CHECK(full.kind == "slice");
    CHECK(full.fixture == "procedural:cube");
    CHECK(full.tier == Tier::Micro);
    CHECK(full.pgo_eligible == std::optional<bool>(true));
    CHECK(full.tags == std::set<std::string> {"procedural", "smoke"});
    CHECK(full.config == Properties {{"layer_height", "0.1"}});
    const CatalogEntry& plain = catalog[1];
    CHECK(plain.fixture.empty());
    CHECK(plain.tier == Tier::Macro);
    CHECK_FALSE(plain.pgo_eligible);
    CHECK(plain.tags.empty());
    CHECK(plain.config.empty());
}

TEST_CASE("a catalog refuses an entry it cannot take whole", "[OrcaBench][Catalog]")
{
    const auto [entry, refusal] = GENERATE(table<std::string, std::string>({
        {R"({"name": "a/b", "kind": "slice", "confg": {}})", "an entry has a field this build does not know: confg"},
        {R"({"kind": "slice"})", "an entry has no name"},
        {R"({"name": "a/b"})", "a/b has no kind"},
        {R"({"name": "a b", "kind": "slice"})", "the catalog entry name 'a b' is empty or has a character outside printable ASCII"},
        {R"({"name": "a/b", "kind": "slice", "tier": "huge"})", "a/b's tier is neither macro nor micro: huge"},
        {R"({"name": "a/b", "kind": "slice", "pgo_eligible": "yes"})", "a/b's pgo_eligible is not true or false"},
        {R"({"name": "a/b", "kind": "slice", "tags": "smoke"})", "a/b's tags are not an array"},
        {R"({"name": "a/b", "kind": "slice", "config": {"layer_height": 0.1}})", "a/b's layer_height is not a string: 0.1"},
    }));
    CAPTURE(entry);
    CHECK_THROWS_WITH(read_catalog(R"({"entries": [)" + entry + "]}"), refusal);
}

TEST_CASE("a catalog is an object holding its entries and nothing else", "[OrcaBench][Catalog]")
{
    const std::string text = GENERATE(as<std::string> {}, "[]", "{}", R"({"entries": {}})", R"({"entries": [], "schema": "1.0"})");
    CAPTURE(text);
    CHECK_THROWS_WITH(read_catalog(text), "a catalog is an object holding an entries array and nothing else");
}

TEST_CASE("a catalog refuses a name that repeats, and text that is not JSON", "[OrcaBench][Catalog]")
{
    CHECK_THROWS_WITH(read_catalog(R"({"entries": [{"name": "a/b", "kind": "slice"}, {"name": "a/b", "kind": "load"}]})"),
                      "a/b appears twice");
    CHECK_THROWS_AS(read_catalog("{"), CatalogError);
}

TEST_CASE("a catalog directory reads its json files in name order and nothing else", "[OrcaBench][Catalog]")
{
    const std::vector<CatalogEntry> catalog = read_catalog_dir(catalogs("split"));
    REQUIRE(catalog.size() == 2);
    CHECK(catalog[0].name == "fake/first");
    CHECK(catalog[1].name == "fake/second");
}

TEST_CASE("a catalog directory refuses a name two of its files share, naming the second file", "[OrcaBench][Catalog]")
{
    CHECK_THROWS_WITH(read_catalog_dir(catalogs("repeated")), EndsWith("b.json: fake/a appears twice"));
    CHECK_THROWS_WITH(read_catalog_dir(catalogs("missing")), ContainsSubstring("cannot read the catalog directory"));
}

TEST_CASE("every slicing workload is a slice of a handy or procedural fixture, named slice/<fixture>/<variant>", "[OrcaBench][Catalog]")
{
    const std::vector<CatalogEntry> catalog = slicing_catalog();
    REQUIRE_FALSE(catalog.empty());
    for (const CatalogEntry& entry : catalog) {
        CAPTURE(entry.name);
        CHECK(entry.kind == "slice");
        CHECK(std::regex_match(entry.name, std::regex("slice/[a-z0-9-]+/[a-z0-9-]+")));
        const bool known_fixture = entry.fixture.rfind("handy:", 0) == 0 || entry.fixture.rfind("procedural:", 0) == 0;
        CHECK(known_fixture);
    }
}

TEST_CASE("the slicing catalog names five handy models under four configurations, and three procedural shapes", "[OrcaBench][Catalog]")
{
    std::map<std::string, Properties> configs;
    for (const CatalogEntry& entry : slicing_catalog())
        configs[entry.name] = entry.config;
    const std::pair<const char*, Properties> variants[] = {
        {"classic", {{"wall_generator", "classic"}}},
        {"arachne", {{"wall_generator", "arachne"}}},
        {"arachne-tree", {{"wall_generator", "arachne"}, {"enable_support", "1"}, {"support_type", "tree(auto)"}}},
        {"classic-normal", {{"wall_generator", "classic"}, {"enable_support", "1"}, {"support_type", "normal(auto)"}}},
    };
    std::map<std::string, Properties> expected = {{"slice/smoke-cube/standard", {}},
                                                  {"slice/peg-grid/standard", {}},
                                                  {"slice/fine-sphere/standard", {}}};
    for (const char* model : {"3dbenchy", "stanford-bunny", "voron-design-cube-v7", "ksr-fdmtest-v4", "orcacube-v2"})
        for (const auto& [variant, config] : variants)
            expected[std::string("slice/") + model + "/" + variant] = config;
    CHECK(configs == expected);
}
