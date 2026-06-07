/**
 * test_sequential_clearance.cpp
 *
 * Catch2 tests for Print::sequential_print_clearance_valid, covering:
 *
 *  Horizontal collision scenarios
 *    1. Two objects far apart             -> no collision
 *    2. Two objects too close             -> collision error + polygons emitted
 *    3. Objects at exact minimum gap      -> no false-positive (boundary condition)
 *    4. XY-clearance mode                 -> asymmetric clearance respected per axis
 *    5. Radius mode distinct from XY      -> radius field used, not clearance_x/y
 *
 *  Vertical height constraint scenarios  (rod and lid clearance)
 *    6.  Single object (always last)      -> never "too tall"
 *    7.  Non-last below lid, no Y-overlap -> no error
 *    8.  Non-last above lid, no Y-overlap -> "too tall" error
 *    9.  Non-last below rod, Y-overlap    -> no error
 *   10.  Non-last above rod, Y-overlap    -> "too tall" error
 *   11.  Above rod, NO Y-overlap          -> uses lid limit (not rod)
 *   12.  Three-object chain               -> only middle triggers error
 *   13.  Non-last Y-band skip: A and C overlap, B doesn't -> A uses rod limit
 *   14.  rod_height > lid_height (misconfigured printer) -> documents current behaviour
 *
 * All tests exercise the *same* expand_clearance_hull() path that GLCanvas3D uses for
 * the drag-preview display -- one canonical formula for both validation and UI.
 */

#include <catch2/catch_all.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Config.hpp"

using namespace Slic3r;

// ---------------------------------------------------------------------------
// Setup helpers
// ---------------------------------------------------------------------------

/// Build a base DynamicPrintConfig for sequential-print tests.
///
/// Radius mode is the default (simplest for reasoning about geometry).
/// Pass ExtruderClearanceType::XY with explicit cx/cy for XY-mode tests.
///
/// @param clearance_r  extruder_clearance_radius (mm) - half applied per side in Radius mode
/// @param lid_h        extruder_clearance_height_to_lid (mm)
/// @param rod_h        extruder_clearance_height_to_rod (mm)
/// @param type         ExtruderClearanceType::Radius or ::XY
/// @param cx           extruder_clearance_x (mm); negative -> use clearance_r
/// @param cy           extruder_clearance_y (mm); negative -> use clearance_r
static DynamicPrintConfig make_seq_config(
    float clearance_r = 20.0f,
    float lid_h       = 50.0f,
    float rod_h       = 25.0f,
    ExtruderClearanceType type = ExtruderClearanceType::Radius,
    float cx = -1.0f,
    float cy = -1.0f)
{
    DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
    cfg.set_key_value("extruder_clearance_type",
        new ConfigOptionEnum<ExtruderClearanceType>(type));
    cfg.set_key_value("extruder_clearance_radius",
        new ConfigOptionFloat(clearance_r));
    cfg.set_key_value("extruder_clearance_x",
        new ConfigOptionFloat(cx < 0.f ? clearance_r : cx));
    cfg.set_key_value("extruder_clearance_y",
        new ConfigOptionFloat(cy < 0.f ? clearance_r : cy));
    cfg.set_key_value("extruder_clearance_height_to_lid",
        new ConfigOptionFloat(lid_h));
    cfg.set_key_value("extruder_clearance_height_to_rod",
        new ConfigOptionFloat(rod_h));
    // Disable skirt so object_skirt_offset() == 0: cleaner geometry.
    cfg.set_key_value("skirts",       new ConfigOptionInt(0));
    cfg.set_key_value("brim_width",   new ConfigOptionFloat(0.0f));
    // Large printable height so the last object is never flagged as too tall.
    cfg.set_key_value("printable_height", new ConfigOptionFloat(200.0f));
    // Default order: objects print in the order they were added to the model.
    cfg.set_key_value("by_object_sequence_order",
        new ConfigOptionEnum<ByObjectSequenceOrder>(ByObjectSequenceOrder::Default));
    return cfg;
}

/// Add one box to the model and set its world-space XY offset.
/// make_cube() produces a mesh with vertices in [0..wx, 0..wy, 0..wz].
/// Setting offset (ox, oy) places the corner of the box at that world position.
/// The box footprint in world space is therefore [ox..ox+wx, oy..oy+wy].
static ModelObject* add_box(Model& model, double wx, double wy, double wz,
                             double ox, double oy)
{
    ModelObject* obj = model.add_object();
    obj->add_volume(make_cube(wx, wy, wz));
    ModelInstance* inst = obj->add_instance();
    inst->set_offset(Vec3d(ox, oy, 0.0));
    return obj;
}

/// Apply the model to the print (without slicing).
/// PrintObject::max_z() and ->height() are populated from model bounding boxes
/// by Print::apply(), so no slicing is required for clearance validation.
///
/// ensure_on_bed() corrects Z if non-flat meshes sink below the bed;
/// auto_assign_extruders() sets volume extruder IDs needed by apply() internals.
static void apply(Print& print, Model& model, const DynamicPrintConfig& cfg)
{
    for (ModelObject* mo : model.objects) {
        mo->ensure_on_bed();
        print.auto_assign_extruders(mo);
    }
    print.apply(model, cfg);
}

// ---------------------------------------------------------------------------
// Horizontal collision scenarios
// ---------------------------------------------------------------------------

TEST_CASE("SequentialClearance: objects far apart produce no collision", "[seq_clearance][collision]")
{
    // Two 20x20mm cubes separated by 100mm - far beyond clearance_radius=20mm.
    // Box A: [0..20, 0..20].  Box B: [120..140, 0..20].  Gap = 100mm >> clearance.
    Model model;
    Print print;
    add_box(model, 20, 20, 20, 0, 0);
    add_box(model, 20, 20, 20, 120, 0);
    apply(print, model, make_seq_config());

    Polygons polys;
    auto result = Print::sequential_print_clearance_valid(print, &polys);

    CHECK(result.string.empty());
    CHECK(polys.empty());
}

TEST_CASE("SequentialClearance: objects too close produce collision error and polygons",
    "[seq_clearance][collision]")
{
    // Two 20x20mm cubes with only a 5mm gap (clearance_radius=20mm requires 20mm gap).
    // Box A: [0..20, 0..20].  Box B: [25..45, 0..20].  Gap = 5mm < 20mm.
    Model model;
    Print print;
    add_box(model, 20, 20, 20, 0, 0);
    add_box(model, 20, 20, 20, 25, 0);
    apply(print, model, make_seq_config(/*clearance_r=*/20.0f));

    Polygons polys;
    auto result = Print::sequential_print_clearance_valid(print, &polys);

    CHECK(!result.string.empty());
    CHECK(!polys.empty());
}

TEST_CASE("SequentialClearance: objects at exact minimum gap produce no false-positive",
    "[seq_clearance][collision]")
{
    // Two 20x20mm cubes with exactly 20mm gap (== clearance_radius).
    // Box A: [0..20, 0..20].  Box B: [40..60, 0..20].  Gap = 20mm.
    // The check hull subtracts 0.1mm tolerance -> a 20mm gap is never flagged.
    Model model;
    Print print;
    add_box(model, 20, 20, 20, 0, 0);
    add_box(model, 20, 20, 20, 40, 0);
    apply(print, model, make_seq_config(/*clearance_r=*/20.0f));

    Polygons polys;
    auto result = Print::sequential_print_clearance_valid(print, &polys);

    CHECK(result.string.empty());
    CHECK(polys.empty());
}

TEST_CASE("SequentialClearance: XY-mode - asymmetric clearance respected in X and Y",
    "[seq_clearance][collision][xy]")
{
    // XY mode: clearance_x=20mm, clearance_y=40mm.
    // The X and Y clearance zones are deliberately different to verify each axis is
    // handled independently and the correct field drives each direction.
    const DynamicPrintConfig cfg = make_seq_config(
        /*clearance_r=*/30.0f, /*lid=*/50.0f, /*rod=*/25.0f,
        ExtruderClearanceType::XY, /*cx=*/20.0f, /*cy=*/40.0f);

    SECTION("X-gap at clearance_x - no collision")
    {
        // 20x20 cubes with gap=20mm in X == clearance_x -> no collision.
        // In XY mode: dx = (clearance_x + 0)/2 - 0.1 = 9.9mm per side.
        // Hull A: x=[-9.9..29.9]. Hull B: x=[30.1..70.1]. Gap = 0.2mm -> no overlap.
        Model model; Print print;
        add_box(model, 20, 20, 20, 0, 0);
        add_box(model, 20, 20, 20, 40, 0); // gap = 40 - 20 = 20mm in X
        apply(print, model, cfg);
        Polygons polys;
        auto result = Print::sequential_print_clearance_valid(print, &polys);
        CHECK(result.string.empty());   // exactly clearance_x -> no X-collision
        CHECK(polys.empty());
    }

    SECTION("Y-gap below clearance_y - collision")
    {
        // 20x20 cubes with gap=20mm in Y < clearance_y=40mm -> collision.
        Model model; Print print;
        add_box(model, 20, 20, 20, 0,  0);
        add_box(model, 20, 20, 20, 0, 40); // gap = 40 - 20 = 20mm in Y < clearance_y=40
        apply(print, model, cfg);
        Polygons polys;
        auto result = Print::sequential_print_clearance_valid(print, &polys);
        CHECK(!result.string.empty());  // 20mm gap < 40mm clearance_y -> collision
        CHECK(!polys.empty());
    }

    SECTION("Y-gap at clearance_y - no collision")
    {
        // 20x20 cubes with gap=40mm in Y == clearance_y -> no collision.
        Model model; Print print;
        add_box(model, 20, 20, 20, 0, 0);
        add_box(model, 20, 20, 20, 0, 60); // gap = 60 - 20 = 40mm in Y = clearance_y
        apply(print, model, cfg);
        Polygons polys;
        auto result = Print::sequential_print_clearance_valid(print, &polys);
        CHECK(result.string.empty());   // exactly clearance_y -> no Y-collision
        CHECK(polys.empty());
    }
}

TEST_CASE("SequentialClearance: Radius mode uses clearance_radius, not clearance_x/y",
    "[seq_clearance][collision][radius]")
{
    // Verify the Radius code path is active: clearance_radius=20mm, clearance_x/y=5mm.
    // At gap=15mm:
    //   Radius mode: hull radius = 10mm/side -> 15mm gap < 20mm threshold -> collision
    //   XY mode:     hull half-width = 2.5mm/side -> 15mm gap > 5mm threshold -> no collision
    // The test expects COLLISION, confirming Radius mode is in effect.
    Model model;
    Print print;
    add_box(model, 20, 20, 20, 0,  0);
    add_box(model, 20, 20, 20, 35, 0); // gap = 35 - 20 = 15mm
    apply(print, model, make_seq_config(
        /*clearance_r=*/20.0f, 50.0f, 25.0f,
        ExtruderClearanceType::Radius, /*cx=*/5.0f, /*cy=*/5.0f));

    Polygons polys;
    auto result = Print::sequential_print_clearance_valid(print, &polys);
    CHECK(!result.string.empty()); // 15mm < clearance_r=20mm -> radius-mode collision
    CHECK(!polys.empty());
}

// ---------------------------------------------------------------------------
// Vertical height constraint scenarios
//
// The validator applies two height limits to non-last objects:
//   hc1 = extruder_clearance_height_to_lid  - default limit
//   hc2 = extruder_clearance_height_to_rod  - tighter limit when a later object
//           shares the same Y band (footprint Y-ranges overlap)
//
// Y-overlap is checked against the DISPLAY hull (clearance-expanded) bbox of
// later objects, not the raw footprint. This means the Y-band threshold is
// effectively clearance_y/2 wider than the raw footprint boundary.
//
// The LAST object printed is always exempt (limit = printable_height).
// ---------------------------------------------------------------------------

TEST_CASE("SequentialClearance height: single object is never too tall", "[seq_clearance][height]")
{
    // A single object is always the last to print -> height limit = printable_height.
    Model model;
    Print print;
    add_box(model, 20, 20, 80, 0, 0); // 80mm tall, well above lid/rod limits
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // last object -> no height constraint
}

TEST_CASE("SequentialClearance height: non-last object below lid height - no error",
    "[seq_clearance][height]")
{
    // A (30mm, printed 1st) and B (10mm, printed last).
    // A and B are far apart in Y -> no Y-overlap -> A's limit = lid_height = 50mm.
    // 30mm < 50mm -> no error.
    Model model;
    Print print;
    add_box(model, 20, 20, 30, 0,   0);   // A: footprint Y=[0..20]
    add_box(model, 20, 20, 10, 0, 200);   // B: footprint Y=[200..220] - no Y-overlap
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // 30mm < lid_height=50mm -> no error
}

TEST_CASE("SequentialClearance height: non-last object above lid height - too tall error",
    "[seq_clearance][height]")
{
    // A (60mm, printed 1st) and B (10mm, printed last). No Y-overlap.
    // A's limit = lid_height = 50mm. 60mm > 50mm -> error.
    Model model;
    Print print;
    add_box(model, 20, 20, 60, 0,   0);  // A: 60mm tall
    add_box(model, 20, 20, 10, 0, 200);  // B: last, no Y-overlap
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    std::vector<std::pair<Polygon, float>> height_polys;
    auto result = Print::sequential_print_clearance_valid(print, nullptr, &height_polys);

    CHECK(!result.string.empty()); // 60mm > lid_height=50mm -> error
    CHECK(!height_polys.empty());  // offending polygon is highlighted
}

TEST_CASE("SequentialClearance height: non-last object below rod height with Y-overlap - no error",
    "[seq_clearance][height]")
{
    // A (20mm tall, Y=0) and B (10mm tall, Y=0). Same Y band -> A's limit = rod_height = 25mm.
    // Separated 100mm in X (no horizontal collision).
    // 20mm < 25mm -> no error.
    Model model;
    Print print;
    add_box(model, 20, 20, 20, 0,   0);  // A: same Y band as B
    add_box(model, 20, 20, 10, 120, 0);  // B: same Y band, printed 2nd
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // 20mm < rod_height=25mm -> no error
}

TEST_CASE("SequentialClearance height: non-last object above rod height with Y-overlap - too tall error",
    "[seq_clearance][height]")
{
    // A (30mm tall, Y=0) and B (10mm tall, Y=0). Same Y band -> A's limit = rod_height = 25mm.
    // 30mm > 25mm -> error.
    Model model;
    Print print;
    add_box(model, 20, 20, 30, 0,   0);  // A: 30mm tall, same Y band
    add_box(model, 20, 20, 10, 120, 0);  // B: same Y band, printed 2nd
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    std::vector<std::pair<Polygon, float>> height_polys;
    auto result = Print::sequential_print_clearance_valid(print, nullptr, &height_polys);

    CHECK(!result.string.empty()); // 30mm > rod_height=25mm -> error
    CHECK(!height_polys.empty());
}

TEST_CASE("SequentialClearance height: above rod height but different Y band - uses lid limit",
    "[seq_clearance][height]")
{
    // A (30mm tall) and B (10mm tall) in DIFFERENT Y bands (180mm apart in Y).
    // No Y-overlap -> A's limit = lid_height = 50mm (rod limit does NOT apply).
    // 30mm < 50mm -> no error.
    Model model;
    Print print;
    add_box(model, 20, 20, 30, 0,   0);  // A: footprint Y=[0..20]
    add_box(model, 20, 20, 10, 0, 200);  // B: footprint Y=[200..220] -> no Y-overlap
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // rod limit not applied; 30mm < lid=50mm -> no error
}

TEST_CASE("SequentialClearance height: three objects - middle too tall triggers error",
    "[seq_clearance][height]")
{
    // Print order: A (10mm, X=0) -> B (30mm, X=120) -> C (10mm, X=240).
    // All three share Y=0 band.
    // B has a later Y-band neighbour (C) -> B's limit = rod_height = 25mm.
    // B is 30mm > 25mm -> error.
    // A has a later Y-band neighbour (B, C) -> A's limit = rod_height.
    // A is 10mm < 25mm -> no error for A.
    // C is last -> no constraint.
    Model model;
    Print print;
    add_box(model, 20, 20, 10, 0,   0);   // A: 10mm, printed 1st
    add_box(model, 20, 20, 30, 120, 0);   // B: 30mm, printed 2nd
    add_box(model, 20, 20, 10, 240, 0);   // C: 10mm, printed 3rd (last)
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    std::vector<std::pair<Polygon, float>> height_polys;
    auto result = Print::sequential_print_clearance_valid(print, nullptr, &height_polys);

    CHECK(!result.string.empty());
    // Only B is flagged (A is below rod limit, C is last).
    CHECK(height_polys.size() == 1);
    // The reported limit is rod_height = 25mm.
    CHECK(height_polys[0].second == Catch::Approx(25.0f).epsilon(0.01));
    // The flagged polygon should be centred near B's position (x=120..140 -> centre x=130).
    // The stored polygon is the clearance-expanded hull, so centre is still near x=130.
    {
        const BoundingBox bb = height_polys[0].first.bounding_box();
        const double cx = unscale<double>(bb.min.x() + bb.max.x()) / 2.0;
        CHECK(cx == Catch::Approx(130.0).margin(25.0)); // within 25mm of B's centre
    }
}

TEST_CASE("SequentialClearance height: non-last Y-band skip - A and C overlap, B does not",
    "[seq_clearance][height]")
{
    // Print order: A (Y=0) -> B (Y=200, different band) -> C (Y=0, same band as A).
    // The inner loop scans ALL later objects for Y-overlap, so A's limit should be
    // rod_height because C (which comes after B) shares A's Y band.
    // A is 30mm tall, rod_height=25mm -> error if the scan reaches C.
    Model model;
    Print print;
    add_box(model, 20, 20, 30, 0,   0);   // A: Y=[0..20],   printed 1st
    add_box(model, 20, 20, 10, 0, 200);   // B: Y=[200..220], printed 2nd (different band)
    add_box(model, 20, 20, 10, 120, 0);   // C: Y=[0..20],   printed 3rd (same band as A)
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    std::vector<std::pair<Polygon, float>> height_polys;
    auto result = Print::sequential_print_clearance_valid(print, nullptr, &height_polys);

    // A's limit = rod_height (because C Y-overlaps A even though B doesn't).
    // 30mm > rod_height=25mm -> error for A.
    CHECK(!result.string.empty());
    CHECK(!height_polys.empty());
    // The reported limit should be rod_height, not lid_height.
    CHECK(height_polys[0].second == Catch::Approx(25.0f).epsilon(0.01));
}

TEST_CASE("SequentialClearance height: rod_height exceeds lid_height - current behaviour",
    "[seq_clearance][height]")
{
    // Misconfigured printer: rod_height=60mm > lid_height=40mm.
    // Two objects in the same Y band; object A is 50mm tall.
    // Y-overlap is detected -> validator applies rod_height=60mm as the limit.
    // 50mm < 60mm -> no error reported, even though 50mm > lid_height=40mm.
    //
    // NOTE: This is the CURRENT behaviour. Ideally the validator would clamp
    // to min(rod_height, lid_height) when rod > lid. This test documents
    // the existing implementation so any future fix is immediately visible.
    Model model;
    Print print;
    add_box(model, 20, 20, 50, 0,   0);   // A: 50mm tall, Y-band shared with B
    add_box(model, 20, 20, 10, 120, 0);   // B: printed 2nd, same Y band
    apply(print, model, make_seq_config(20.0f, /*lid=*/40.0f, /*rod=*/60.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    // Current: Y-overlap -> rod_height=60mm -> 50 < 60 -> no error.
    // If behaviour is corrected (using min(rod,lid)=40mm): 50 > 40 -> error.
    CHECK(result.string.empty()); // documents current (possibly surprising) behaviour
}
