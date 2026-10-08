#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "slicer/Fixtures.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

#include <stdexcept>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Bench;
using Catch::Matchers::EndsWith;
using Catch::Matchers::WithinAbs;

namespace {

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

} // namespace

TEST_CASE("a procedural fixture is one object with one instance, centered on the bed", "[OrcaBench][Fixtures]")
{
    const FixtureModel fixture = load_fixture("procedural:smoke-cube");
    REQUIRE(fixture.model);
    REQUIRE(fixture.model->objects.size() == 1);
    CHECK(fixture.model->objects.front()->instances.size() == 1);
    const BoundingBoxf3 box = fixture.model->bounding_box_exact();
    CHECK_THAT(box.center().x(), WithinAbs(175., 1e-6));
    CHECK_THAT(box.center().y(), WithinAbs(175., 1e-6));
    CHECK_THAT(box.min.z(), WithinAbs(0., 1e-6));
    CHECK_THAT(box.size().z(), WithinAbs(20., 1e-6));
}

TEST_CASE("a handy fixture reads the model the app offers, and a missing one names the path it looked for", "[OrcaBench][Fixtures]")
{
    const TreeResources resources;
    const FixtureModel  voron = load_fixture("handy:Voron_Design_Cube_v7.drc");
    REQUIRE(voron.model);
    CHECK_FALSE(voron.model->objects.empty());
    CHECK(voron.unavailable.empty());

    const FixtureModel missing = load_fixture("handy:missing.drc");
    CHECK_FALSE(missing.model);
    CHECK_THAT(missing.unavailable, EndsWith("missing.drc is missing"));
}

TEST_CASE("a fixture id that names no fixture is refused", "[OrcaBench][Fixtures]")
{
    const std::string id = GENERATE(as<std::string> {}, "smoke-cube", "procedural:sphere", "handy");
    CAPTURE(id);
    CHECK_THROWS_AS(load_fixture(id), std::invalid_argument);
}

TEST_CASE("the hermetic config sets the bed, the layer change G-code and no object labels, then the entry's keys", "[OrcaBench][Fixtures]")
{
    const DynamicPrintConfig config = hermetic_config({{"layer_height", "0.3"}, {"z_hop_types", "Spiral Lift"}, {"nozzle_type", "brass"}});
    CHECK(config.opt_serialize("printable_area") == "0x0,350x0,350x350,0x350");
    CHECK(config.opt_string("layer_change_gcode") == "G92 E0");
    CHECK_FALSE(config.opt_bool("gcode_label_objects"));
    CHECK_THAT(config.opt_float("printable_height"), WithinAbs(350., 1e-9));
    CHECK_THAT(config.opt_float("layer_height"), WithinAbs(0.3, 1e-9));
    CHECK(config.opt_serialize("z_hop_types") == "Spiral Lift");
    CHECK(config.opt_serialize("nozzle_type") == "brass");
    CHECK(config.opt_serialize("retract_lift_enforce") == "All Surfaces");
    CHECK_THROWS(hermetic_config({{"no_such_key", "1"}}));
}
