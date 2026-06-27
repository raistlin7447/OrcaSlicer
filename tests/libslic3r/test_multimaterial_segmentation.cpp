#include <catch2/catch_all.hpp>

// MultiMaterialSegmentation.hpp declares boost::polygon traits for ColoredLine, so its
// geometry/boost dependencies must be included first.
#include <boost/polygon/polygon.hpp>
#include "libslic3r/Line.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/MultiMaterialSegmentation.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

TEST_CASE("Multi-material segmentation resolves the outer-wall line width", "[MultiMaterialSegmentation][Regression]")
{
    struct Case
    {
        std::string description;
        double      outer_value;
        bool        outer_percent;
        double      line_value;
        bool        line_percent;
        double      nozzle_diameter;
        double      expected;
    };

    auto c = GENERATE(values<Case>({
        {"absolute outer-wall width is used as-is",   0.6, false, 0.42, false, 0.4, 0.6},
        {"percent outer-wall width uses the nozzle",  120, true,  0.42, false, 0.5, 0.6},
        {"zero outer-wall width uses the line width", 0,   false, 0.5,  false, 0.4, 0.5},
        {"zero outer-wall width uses a percent line", 0,   false, 100,  true,  0.5, 0.5},
        {"zero width falls back to auto",             0,   false, 0,    false, 0.4, Flow::auto_extrusion_width(frExternalPerimeter, 0.4)},
        {"the auto fallback scales with the nozzle",  0,   false, 0,    false, 0.6, Flow::auto_extrusion_width(frExternalPerimeter, 0.6)},
    }));

    DYNAMIC_SECTION(c.description)
    {
        PrintObjectConfig object_config;
        PrintRegionConfig region_config;
        region_config.outer_wall_line_width = ConfigOptionFloatOrPercent(c.outer_value, c.outer_percent);
        object_config.line_width            = ConfigOptionFloatOrPercent(c.line_value, c.line_percent);

        REQUIRE_THAT(resolve_outer_wall_line_width(region_config, object_config, c.nozzle_diameter),
                     Catch::Matchers::WithinAbs(c.expected, 1e-9));
    }
}
