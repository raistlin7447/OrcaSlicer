#include <catch2/catch_all.hpp>

#include <fstream>
#include <memory>

#include <boost/filesystem.hpp>

#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

using Catch::Matchers::WithinAbs;

SCENARIO("Origin manipulation", "[GCode]") {
	Slic3r::GCode gcodegen;
	WHEN("set_origin to (10,0)") {
    	gcodegen.set_origin(Vec2d(10,0));
    	REQUIRE(gcodegen.origin() == Vec2d(10, 0));
    }
	WHEN("set_origin to (10,0) and translate by (5, 5)") {
		gcodegen.set_origin(Vec2d(10,0));
		gcodegen.set_origin(gcodegen.origin() + Vec2d(5, 5));
		THEN("origin returns reference to point") {
    		REQUIRE(gcodegen.origin() == Vec2d(15,5));
    	}
    }
}

// Orca: machine_additional_prepare_time is injected into the time estimate at the start-gcode ->
// first-print-move seam. These tests drive GCodeProcessor over canned G-code and check the injected
// time lands in both the prepare time and the total, and that it is injected only once.
namespace {

struct EstimatedTimes { float total; float prepare; };

// Normal layout: a few start-gcode (Custom) moves, the prepare->print seam, then body moves.
const char* const k_prepare_seam_gcode =
    ";TYPE:Custom\n"
    "G1 Z0.2 F600\n"
    "G1 X10 Y10 F3000\n"
    "G1 X20 Y10 E1 F1200\n"
    ";TYPE:Inner wall\n"
    "G1 X20 Y20 E2 F1200\n"
    "G1 X10 Y20 E3 F1200\n"
    "G1 X10 Y10 E4 F1200\n";

// Degenerate layout that crosses the start-gcode -> print boundary TWICE: the Custom regions contain
// only firmware retracts (G10/G11), which create prepare blocks without counting as real G1 moves, so
// m_g1_line_id stays 0 and the start-custom-gcode flag toggles true->false twice. The additional time
// must still be injected only once.
const char* const k_double_transition_gcode =
    ";TYPE:Custom\n"
    "G10\n"
    "G11\n"
    ";TYPE:Inner wall\n"
    ";TYPE:Custom\n"
    "G10\n"
    "G11\n"
    ";TYPE:Inner wall\n"
    "G1 X10 Y10 F3000\n"
    "G1 X20 Y20 E1 F1200\n"
    "G1 X10 Y20 E2 F1200\n";

EstimatedTimes estimate(const char* gcode, float additional_s)
{
    FullPrintConfig config; // default-constructed with all defaults
    config.machine_additional_prepare_time.value = additional_s;
    config.retraction_length.values = { 2.0 };  // ensure G10/G11 emit real retract moves
    config.retraction_speed.values  = { 30.0 };

    GCodeProcessor gp;
    GCodeProcessor::s_IsBBLPrinter = false; // use the "TYPE:" role tags in the canned G-code
    gp.apply_config(config); // public PrintConfig overload (FullPrintConfig is-a PrintConfig)

    namespace fs = boost::filesystem;
    const fs::path tmp = fs::temp_directory_path() / fs::unique_path("orca-prep-%%%%%%%%.gcode");
    { std::ofstream out(tmp.string()); out << gcode; }
    gp.process_file(tmp.string());
    boost::system::error_code ec;
    fs::remove(tmp, ec);

    const auto& mode = gp.get_result().print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    return { mode.time, mode.prepare_time };
}

} // namespace

SCENARIO("machine_additional_prepare_time is injected into the time estimate", "[GCode]") {
    const EstimatedTimes base = estimate(k_prepare_seam_gcode, 0.0f);

    GIVEN("a baseline estimate with no additional prepare time") {
        THEN("the canned G-code produces a non-zero estimate") {
            REQUIRE(base.total > 0.0f);
        }

        WHEN("600 s of additional prepare time is set") {
            const EstimatedTimes adjusted = estimate(k_prepare_seam_gcode, 600.0f);

            THEN("the total grows by 600 s") {
                REQUIRE_THAT(adjusted.total - base.total, WithinAbs(600.0, 1.0));
            }
            THEN("the prepare time grows by 600 s (the injected time counts as prepare)") {
                REQUIRE_THAT(adjusted.prepare - base.prepare, WithinAbs(600.0, 1.0));
            }
            THEN("the model (non-prepare) time is unchanged") {
                const float base_model     = base.total - base.prepare;
                const float adjusted_model = adjusted.total - adjusted.prepare;
                REQUIRE_THAT(adjusted_model - base_model, WithinAbs(0.0, 1.0));
            }
        }
    }
}

SCENARIO("machine_additional_prepare_time is injected only once across multiple start-gcode transitions", "[GCode]") {
    const EstimatedTimes base = estimate(k_double_transition_gcode, 0.0f);

    GIVEN("a G-code that crosses the start-gcode boundary more than once") {
        WHEN("600 s of additional prepare time is set") {
            const EstimatedTimes adjusted = estimate(k_double_transition_gcode, 600.0f);

            THEN("the additional time is added exactly once, not per transition") {
                REQUIRE_THAT(adjusted.total - base.total, WithinAbs(600.0, 1.0));
                REQUIRE_THAT(adjusted.prepare - base.prepare, WithinAbs(600.0, 1.0));
            }
        }
    }
}
