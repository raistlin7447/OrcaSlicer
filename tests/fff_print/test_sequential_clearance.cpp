/**
 * test_sequential_clearance.cpp
 *
 * Catch2 tests for Print::sequential_print_clearance_valid, covering:
 *
 *  Horizontal collision scenarios
 *    1. Two objects far apart        → no collision
 *    2. Two objects too close        → collision error + polygons emitted
 *    3. Objects at exact minimum gap → no false-positive (boundary condition)
 *    4. XY-clearance mode collision  → asymmetric clearance respected
 *
 *  Vertical height constraint scenarios  (rod and lid clearance)
 *    5. Single object (always last)  → never "too tall"
 *    6. Non-last object below lid height, no Y-overlap → no error
 *    7. Non-last object above lid height, no Y-overlap → "too tall" error
 *    8. Non-last object below rod height, Y-overlap with later object → no error
 *    9. Non-last object above rod height, Y-overlap with later object → "too tall" error
 *   10. Non-last object above rod height, NO Y-overlap  → uses lid limit, no error
 *         (rod height limit only applies when objects share a Y band)
 *
 * All tests exercise the *same* expand_clearance_hull() path that GLCanvas3D uses for
 * the drag-preview display -one canonical formula for both validation and UI.
 */

#include <catch2/catch_all.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

// ─── Setup helpers ────────────────────────────────────────────────────────────

/// Build a base DynamicPrintConfig for sequential-print tests.
/// Clearance values are in radius mode (simplest for reasoning about geometry).
/// @param clearance_r  extruder_clearance_radius (mm) -half applied per side
/// @param lid_h        extruder_clearance_height_to_lid (mm)
/// @param rod_h        extruder_clearance_height_to_rod (mm)
static DynamicPrintConfig make_seq_config(
    float clearance_r = 20.0f,
    float lid_h       = 50.0f,
    float rod_h       = 25.0f)
{
    DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
    // Use radius mode so clearance geometry is simple and symmetric.
    cfg.set_key_value("extruder_clearance_type",
        new ConfigOptionEnum<ExtruderClearanceType>(ExtruderClearanceType::Radius));
    cfg.set_key_value("extruder_clearance_radius",
        new ConfigOptionFloat(clearance_r));
    cfg.set_key_value("extruder_clearance_x",
        new ConfigOptionFloat(clearance_r));
    cfg.set_key_value("extruder_clearance_y",
        new ConfigOptionFloat(clearance_r));
    cfg.set_key_value("extruder_clearance_height_to_lid",
        new ConfigOptionFloat(lid_h));
    cfg.set_key_value("extruder_clearance_height_to_rod",
        new ConfigOptionFloat(rod_h));
    // Disable skirt so object_skirt_offset() == 0: cleaner geometry.
    cfg.set_key_value("skirts", new ConfigOptionInt(0));
    cfg.set_key_value("brim_width", new ConfigOptionFloat(0.0f));
    // Large printable height so the last object is never flagged as too tall.
    cfg.set_key_value("printable_height", new ConfigOptionFloat(200.0f));
    // List order: objects print in the order they were added to the model.
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
/// print_object->max_z() and ->height() are set from model bounding boxes,
/// so no slicing is required for clearance validation tests.
static void apply(Print& print, Model& model, const DynamicPrintConfig& cfg)
{
    for (ModelObject* mo : model.objects) {
        mo->ensure_on_bed();
        print.auto_assign_extruders(mo);
    }
    print.apply(model, cfg);
}

// ─── Collision detection scenarios ───────────────────────────────────────────

TEST_CASE("SequentialClearance: objects far apart produce no collision", "[seq_clearance][collision]")
{
    // Two 20×20mm cubes separated by 100mm -far beyond clearance_radius=20mm.
    // Box A footprint: [0..20, 0..20].  Box B footprint: [120..140, 0..20].
    // Gap = 100mm >> clearance, so no collision should be reported.
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
    // Two 20×20mm cubes with only a 5mm gap (clearance_radius=20mm requires 20mm gap).
    // Box A: [0..20, 0..20].  Box B: [25..45, 0..20].  Gap = 5mm < 20mm.
    Model model;
    Print print;
    add_box(model, 20, 20, 20, 0, 0);
    add_box(model, 20, 20, 20, 25, 0);
    apply(print, model, make_seq_config(/*clearance_r=*/20.0f));

    Polygons polys;
    auto result = Print::sequential_print_clearance_valid(print, &polys);

    // Both a text error and highlight polygons should be produced.
    CHECK(!result.string.empty());
    CHECK(!polys.empty());
}

TEST_CASE("SequentialClearance: objects at exact minimum gap produce no false-positive",
    "[seq_clearance][collision]")
{
    // Two 20×20mm cubes with exactly 20mm gap (== clearance_radius).
    // Box A: [0..20, 0..20].  Box B: [40..60, 0..20].  Gap = 20mm.
    // The check hull subtracts 0.1mm tolerance → no false-positive at the boundary.
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

TEST_CASE("SequentialClearance: XY-mode -asymmetric clearance respected in X and Y",
    "[seq_clearance][collision][xy]")
{
    // XY mode: clearance_x=20mm, clearance_y=40mm.
    // Two 20×20mm cubes separated by 20mm in X -no X-collision (gap equals clearance_x).
    // The same separation in Y would be a collision (gap < clearance_y).
    DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
    cfg.set_key_value("extruder_clearance_type",
        new ConfigOptionEnum<ExtruderClearanceType>(ExtruderClearanceType::XY));
    cfg.set_key_value("extruder_clearance_x",  new ConfigOptionFloat(20.0f));
    cfg.set_key_value("extruder_clearance_y",  new ConfigOptionFloat(40.0f));
    cfg.set_key_value("extruder_clearance_radius", new ConfigOptionFloat(20.0f));
    cfg.set_key_value("extruder_clearance_height_to_lid", new ConfigOptionFloat(50.0f));
    cfg.set_key_value("extruder_clearance_height_to_rod", new ConfigOptionFloat(25.0f));
    cfg.set_key_value("skirts",         new ConfigOptionInt(0));
    cfg.set_key_value("brim_width",     new ConfigOptionFloat(0.0f));
    cfg.set_key_value("printable_height", new ConfigOptionFloat(200.0f));
    cfg.set_key_value("by_object_sequence_order",
        new ConfigOptionEnum<ByObjectSequenceOrder>(ByObjectSequenceOrder::Default));

    // ── Separated exactly clearance_x (20mm) in X → no collision ──────────
    {
        Model model;
        Print print;
        add_box(model, 20, 20, 20, 0, 0);
        add_box(model, 20, 20, 20, 40, 0); // gap = 40-20 = 20mm in X
        apply(print, model, cfg);

        Polygons polys;
        auto result = Print::sequential_print_clearance_valid(print, &polys);
        CHECK(result.string.empty());   // exactly clearance_x → no X-collision
        CHECK(polys.empty());
    }

    // ── Separated only clearance_y/2 (20mm) in Y → Y-collision ───────────
    {
        Model model;
        Print print;
        add_box(model, 20, 20, 20, 0,  0);
        add_box(model, 20, 20, 20, 0, 40); // gap = 40-20 = 20mm in Y < clearance_y=40
        apply(print, model, cfg);

        Polygons polys;
        auto result = Print::sequential_print_clearance_valid(print, &polys);
        CHECK(!result.string.empty());  // 20mm gap < 40mm clearance_y → collision
        CHECK(!polys.empty());
    }

    // ── Separated exactly clearance_y (40mm) in Y → no collision ──────────
    {
        Model model;
        Print print;
        add_box(model, 20, 20, 20, 0, 0);
        add_box(model, 20, 20, 20, 0, 60); // gap = 60-20 = 40mm in Y = clearance_y
        apply(print, model, cfg);

        Polygons polys;
        auto result = Print::sequential_print_clearance_valid(print, &polys);
        CHECK(result.string.empty());   // exactly clearance_y → no Y-collision
        CHECK(polys.empty());
    }
}

// ─── Vertical height constraint scenarios ────────────────────────────────────
//
// The validator applies two height limits:
//   hc1 = extruder_clearance_height_to_lid   -default limit for non-last objects
//   hc2 = extruder_clearance_height_to_rod   -tighter limit when a later object
//           shares the same Y band (Y ranges overlap after stripping clearance)
//
// The LAST object printed is never checked (limit = printable_height).
//
// Rod-height Y-overlap detection:
//   Objects printed in the same Y band (their actual footprints share a Y range)
//   impose the tighter rod limit on all earlier objects in that band.
//   Objects in different Y bands (no actual Y-range overlap) use only the lid limit.

TEST_CASE("SequentialClearance height: single object is never too tall", "[seq_clearance][height]")
{
    // A single object is always the last to print → height limit = printable_height.
    // Even a very tall object should not produce a "too tall" error when alone.
    Model model;
    Print print;
    add_box(model, 20, 20, 80, 0, 0); // 80mm tall
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // last object → no height constraint
}

TEST_CASE("SequentialClearance height: non-last object below lid height -no error",
    "[seq_clearance][height]")
{
    // Object A (30mm, printed first) and object B (10mm, printed last).
    // A and B are far apart in Y → no Y-overlap → A's limit = lid_height = 50mm.
    // A is 30mm < 50mm → no error.
    Model model;
    Print print;
    add_box(model, 20, 20, 30, 0,   0);   // A: printed 1st; footprint Y=[0..20]
    add_box(model, 20, 20, 10, 0, 200);   // B: printed 2nd; footprint Y=[200..220] -no Y-overlap with A
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // 30mm < lid_height=50mm → no error
}

TEST_CASE("SequentialClearance height: non-last object above lid height -too tall error",
    "[seq_clearance][height]")
{
    // Object A (60mm tall) printed first, object B (10mm) printed last.
    // No Y-overlap → A's limit = lid_height = 50mm.
    // A is 60mm > 50mm → "too tall" error.
    Model model;
    Print print;
    add_box(model, 20, 20, 60, 0,   0);  // A: 60mm tall, printed 1st
    add_box(model, 20, 20, 10, 0, 200);  // B: printed 2nd (last), no Y-overlap
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    std::vector<std::pair<Polygon, float>> height_polys;
    auto result = Print::sequential_print_clearance_valid(print, nullptr, &height_polys);

    CHECK(!result.string.empty()); // 60mm > lid_height=50mm → error
    CHECK(!height_polys.empty());  // the offending polygon is highlighted
}

TEST_CASE("SequentialClearance height: non-last object below rod height with Y-overlap -no error",
    "[seq_clearance][height]")
{
    // Two objects in the SAME Y row (Y-overlap): A (20mm tall) and B (10mm tall).
    // A is printed first and has a later neighbour B in the same Y band
    // → A's limit = rod_height = 25mm.
    // A is 20mm < 25mm → no error.
    Model model;
    Print print;
    // Both objects at Y=0; separated by 100mm in X (no horizontal collision).
    add_box(model, 20, 20, 20, 0,   0);  // A: 20mm tall, same Y band, printed 1st
    add_box(model, 20, 20, 10, 120, 0);  // B: 10mm tall, same Y band, printed 2nd
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // 20mm < rod_height=25mm → no error
}

TEST_CASE("SequentialClearance height: non-last object above rod height with Y-overlap -too tall error",
    "[seq_clearance][height]")
{
    // Two objects in the SAME Y row: A (30mm tall) and B (10mm tall).
    // A's limit = rod_height = 25mm because B Y-overlaps with A.
    // A is 30mm > 25mm → "too tall" error.
    Model model;
    Print print;
    add_box(model, 20, 20, 30, 0,   0);  // A: 30mm tall, same Y band, printed 1st
    add_box(model, 20, 20, 10, 120, 0);  // B: 10mm tall, same Y band, printed 2nd
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    std::vector<std::pair<Polygon, float>> height_polys;
    auto result = Print::sequential_print_clearance_valid(print, nullptr, &height_polys);

    CHECK(!result.string.empty()); // 30mm > rod_height=25mm → error
    CHECK(!height_polys.empty());
}

TEST_CASE("SequentialClearance height: above rod height but different Y band -uses lid limit",
    "[seq_clearance][height]")
{
    // Object A (30mm tall) printed first; object B (10mm) printed second,
    // but B is in a DIFFERENT Y band (no Y-overlap with A).
    // → A's limit = lid_height = 50mm (not rod_height=25mm).
    // A is 30mm < 50mm → no error.
    Model model;
    Print print;
    add_box(model, 20, 20, 30, 0,   0);  // A: footprint Y=[0..20]
    add_box(model, 20, 20, 10, 0, 200);  // B: footprint Y=[200..220] → no Y-overlap
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    auto result = Print::sequential_print_clearance_valid(print);
    CHECK(result.string.empty()); // 30mm < lid_height=50mm → no error (rod limit not applied)
}

TEST_CASE("SequentialClearance height: three objects -middle too tall triggers error",
    "[seq_clearance][height]")
{
    // Print order (list order): A (short, Y=0) → B (tall, Y=0 -same band as A) → C (short, Y=0).
    // B has a later Y-overlapping neighbour (C) → B's limit = rod_height = 25mm.
    // B is 30mm > 25mm → error.
    // A has a later Y-overlapping neighbour (B and C) → A's limit = rod_height = 25mm.
    // A is 10mm < 25mm → no error for A.
    // C is last → no height constraint.
    Model model;
    Print print;
    add_box(model, 20, 20, 10, 0,   0);   // A: 10mm, printed 1st, same Y band
    add_box(model, 20, 20, 30, 120, 0);   // B: 30mm, printed 2nd, same Y band
    add_box(model, 20, 20, 10, 240, 0);   // C: 10mm, printed 3rd (last), same Y band
    apply(print, model, make_seq_config(20.0f, /*lid=*/50.0f, /*rod=*/25.0f));

    std::vector<std::pair<Polygon, float>> height_polys;
    auto result = Print::sequential_print_clearance_valid(print, nullptr, &height_polys);

    // B triggers the error; the report should mention the too-tall object.
    CHECK(!result.string.empty());
    CHECK(!height_polys.empty());

    // Only one object (B) should be flagged.
    CHECK(height_polys.size() == 1);
    // The height limit for B should be rod_height = 25mm.
    float reported_height = height_polys[0].second;
    CHECK(reported_height == Catch::Approx(25.0f).epsilon(0.01));
}
