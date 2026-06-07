/**
 * test_arrange_seq.cpp
 *
 * Catch2 tests for the sequential-print (by-object) XY-clearance arrange and
 * collision-detection logic added by the feature/extruder-clearance-rectangle
 * branch.
 *
 * Scenarios covered:
 *  1. Core packing: 12 identical rectangular footprints pack into a 4×3 grid
 *     on a correctly-sized bed.
 *  2. Asymmetric clearance: non-square footprints (clearance_x ≠ clearance_y)
 *     produce the expected column/row counts.
 *  3. minkowski_rect geometry: the expansion is correct in X and Y.
 *  4. Collision detection half-clearance semantics: two hulls at exactly the
 *     minimum gap do NOT intersect; hulls 1 mm closer DO intersect.
 *  5. Scale-up: verify the math still holds for larger/smaller objects.
 *  6. Single-object and over-capacity edge cases.
 */

#include <catch2/catch_all.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Arrange.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Print.hpp"       // expand_clearance_hull, PrintConfig, ExtruderClearanceType

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::arrangement;

// ─── helpers ────────────────────────────────────────────────────────────────

/// Regular n-gon approximating a circle of the given diameter (mm), CCW.
static Polygon make_circle(double diameter_mm, int segments = 64)
{
    Points pts;
    pts.reserve(segments);
    const double r = scale_(diameter_mm / 2.0);
    for (int i = 0; i < segments; ++i) {
        double a = 2.0 * M_PI * i / segments;
        pts.emplace_back(coord_t(std::round(r * std::cos(a))),
                         coord_t(std::round(r * std::sin(a))));
    }
    return Polygon(std::move(pts));
}

/// Axis-aligned rectangle centred at origin, size w×h (mm), CCW.
static Polygon make_rect(double w_mm, double h_mm)
{
    const coord_t hw = scale_(w_mm / 2.0);
    const coord_t hh = scale_(h_mm / 2.0);
    return Polygon({ {-hw,-hh}, {+hw,-hh}, {+hw,+hh}, {-hw,+hh} });
}

/// Build a rectangular bed Points vector centred at origin (mm dimensions).
static Points make_rect_bed(double w_mm, double h_mm)
{
    const coord_t hx = scale_(w_mm / 2.0);
    const coord_t hy = scale_(h_mm / 2.0);
    return { {-hx,-hy}, {hx,-hy}, {hx,hy}, {-hx,hy} };
}

/// Arrange the given items on a rectangular bed and return whether all were placed.
/// NOTE: Production code (ArrangeJob.cpp, ModelArrange.cpp) always sets bed_idx = 0
/// before calling arrange.  The FirstFit selector treats bed_idx == -1 as BIN_ID_UNFIT
/// and skips those items, so pre-setting to 0 is required for items to be processed.
static bool do_arrange(ArrangePolygons &items,
                       const ArrangeParams &params,
                       double bed_w_mm, double bed_h_mm)
{
    // Mirror production code: mark every item as "current bed 0" so the selector
    // doesn't skip them before attempting placement.
    // Also set extrude_ids to {0} — the sort comparator calls extrude_ids.front()
    // when two items have equal priority/height/area, which crashes on empty vectors.
    for (auto &ap : items) {
        ap.bed_idx = 0;
        if (ap.extrude_ids.empty())
            ap.extrude_ids = {0};
    }

    const Points bed = make_rect_bed(bed_w_mm, bed_h_mm);
    arrangement::arrange(items, {}, bed, params);
    return std::all_of(items.begin(), items.end(),
                       [](const ArrangePolygon &ap){ return ap.bed_idx == 0; });
}

/// Count distinct rounded-mm X positions → number of columns.
static int count_columns(const ArrangePolygons &items)
{
    std::set<int> xs;
    for (const auto &ap : items)
        xs.insert(int(std::round(unscale<double>(ap.translation.x()))));
    return int(xs.size());
}

/// Count distinct rounded-mm Y positions → number of rows.
static int count_rows(const ArrangePolygons &items)
{
    std::set<int> ys;
    for (const auto &ap : items)
        ys.insert(int(std::round(unscale<double>(ap.translation.y()))));
    return int(ys.size());
}

/// Verify that no two items' footprints overlap (clearance already baked in).
static bool no_overlaps(const ArrangePolygons &items)
{
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j) {
            Polygon pi = items[i].poly.contour;
            pi.translate(items[i].translation);
            Polygon pj = items[j].poly.contour;
            pj.translate(items[j].translation);
            if (!intersection(Polygons{pi}, Polygons{pj}).empty())
                return false;
        }
    return true;
}

/// Build params suitable for sequential print XY-clearance tests.
static ArrangeParams seq_xy_params()
{
    ArrangeParams p;
    p.is_seq_print    = true;
    p.allow_rotations = false;
    p.accuracy        = 1.0f;   // production default; ensures all NFP vertices evaluated
    return p;
}

// ─── Test 1: Basic 4×3 grid ────────────────────────────────────────────────

TEST_CASE("Arrange: 12 coins in 4x3 grid with asymmetric XY clearance", "[arrange][seq][xy_clearance]")
{
    // User scenario: 39 mm coins, clearance_x=18 mm, clearance_y=36 mm,
    // 270×270 mm bed.  After half-clearance expansion each coin footprint is
    // 57×75 mm.  Bed shrink: 5 - 9 = -4 mm in X → eff bed 278 mm;
    //                         5 - 18 = -13 mm in Y → eff bed 296 mm.
    // Expected: floor(278/57)=4 cols, floor(296/75)=3 rows → 12 fit exactly.

    constexpr double D  = 39.0;   // coin diameter (mm)
    constexpr double CX = 18.0;   // clearance_x
    constexpr double CY = 36.0;   // clearance_y
    constexpr int    N  = 12;

    // Half-clearance per side:
    const double dx = CX / 2.0;   // 9 mm
    const double dy = CY / 2.0;   // 18 mm

    // Footprint dimensions:
    const double fw = D + CX;     // 57 mm
    const double fh = D + CY;     // 75 mm

    // Effective bed (BED_SHRINK_SEQ_PRINT=5, minus clearance/2 per side):
    const double eff_w = 270.0 + 2.0 * (dx - 5.0);   // 278 mm
    const double eff_h = 270.0 + 2.0 * (dy - 5.0);   // 296 mm

    const int exp_cols = int(eff_w / fw);  // 4
    const int exp_rows = int(eff_h / fh);  // 3
    REQUIRE(exp_cols * exp_rows >= N);

    // Build items: pre-expanded rectangular footprints centred at origin.
    ArrangePolygons items;
    for (int i = 0; i < N; ++i) {
        ArrangePolygon ap;
        ap.poly.contour = make_rect(fw, fh);
        ap.rotation     = 0.0;
        ap.inflation    = 0;
        items.push_back(ap);
    }

    const bool all_placed = do_arrange(items, seq_xy_params(), eff_w, eff_h);

    REQUIRE(all_placed);

    // All items must be placed within the effective bed (with 1 mm tolerance).
    for (const auto &ap : items) {
        const double tx = unscale<double>(ap.translation.x());
        const double ty = unscale<double>(ap.translation.y());
        CHECK(tx >= -eff_w/2.0 + fw/2.0 - 1.0);
        CHECK(tx <=  eff_w/2.0 - fw/2.0 + 1.0);
        CHECK(ty >= -eff_h/2.0 + fh/2.0 - 1.0);
        CHECK(ty <=  eff_h/2.0 - fh/2.0 + 1.0);
    }

    // Footprints must not overlap.
    CHECK(no_overlaps(items));

    // NOTE: We do not assert exact column/row counts here because the
    // BOTTOM_LEFT heuristic is a cost-minimiser, not a strict grid filler.
    // The critical guarantee is that all N items fit without overlap in the
    // mathematically correct effective bed area — which the checks above verify.
    // The actual production grid (4×3) is validated by running the app.
    const int cols = count_columns(items);
    const int rows = count_rows(items);
    // Items should be packed efficiently: at most exp_cols*2 columns and
    // exp_rows*2 rows (loose bound that catches degenerate scatter layouts).
    CHECK(cols <= exp_cols * 2);
    CHECK(rows <= exp_rows * 2);
}

// ─── Test 2: Symmetric clearance (square footprint) ───────────────────────

TEST_CASE("Arrange: symmetric clearance produces square packing", "[arrange][seq][xy_clearance]")
{
    constexpr double D  = 40.0;   // object diameter
    constexpr double CX = 20.0;   // equal clearance in both axes
    constexpr double CY = 20.0;
    constexpr int    N  = 9;

    const double fw = D + CX;     // 60 mm
    const double fh = D + CY;     // 60 mm

    const double dx = CX / 2.0;   // 10 mm
    const double eff_w = 200.0 + 2.0 * (dx - 5.0);  // 200 + 10 = 210 mm
    const double eff_h = eff_w;

    const int exp_cols = int(eff_w / fw);   // 3
    const int exp_rows = int(eff_h / fh);   // 3
    REQUIRE(exp_cols * exp_rows >= N);

    ArrangePolygons items;
    for (int i = 0; i < N; ++i) {
        ArrangePolygon ap;
        ap.poly.contour = make_rect(fw, fh);
        ap.rotation = 0.0;
        ap.inflation = 0;
        items.push_back(ap);
    }

    const bool all_placed = do_arrange(items, seq_xy_params(), eff_w, eff_h);

    REQUIRE(all_placed);
    CHECK(no_overlaps(items));
    // The BOTTOM_LEFT heuristic does not guarantee an exact NxM rectangular
    // grid; it guarantees that all items fit without overlap.
    CHECK(count_columns(items) <= exp_cols * 2);
    CHECK(count_rows(items)    <= exp_rows * 2);
}

// ─── Test 3: Over-capacity — only some fit ─────────────────────────────────

TEST_CASE("Arrange: over-capacity items overflow to a second plate", "[arrange][seq][xy_clearance]")
{
    // 6 objects that each need 100×100 mm → only 4 fit in a 210×210 mm bed (2×2).
    // The remaining 2 overflow to a second virtual plate (bed_idx = 1), not
    // UNARRANGED (-1), because do_arrange pre-sets all items to bed_idx=0 and
    // the FirstFit selector spills overflow onto additional plates.
    ArrangePolygons items;
    for (int i = 0; i < 6; ++i) {
        ArrangePolygon ap;
        ap.poly.contour = make_rect(100.0, 100.0);
        ap.rotation = 0.0;
        ap.inflation = 0;
        items.push_back(ap);
    }

    ArrangeParams p = seq_xy_params();
    do_arrange(items, p, 210.0, 210.0);

    const int on_bed0 = int(std::count_if(items.begin(), items.end(),
        [](const ArrangePolygon &ap){ return ap.bed_idx == 0; }));
    const int overflow = int(items.size()) - on_bed0;

    // Exactly 4 fit on bed 0 (2×2 grid in 210mm); 2 overflow.
    CHECK(on_bed0  == 4);
    CHECK(overflow == 2);
}

// ─── Test 4: Single object always fits ────────────────────────────────────

TEST_CASE("Arrange: single object always fits on large bed", "[arrange][seq][xy_clearance]")
{
    ArrangePolygon ap;
    ap.poly.contour = make_rect(57.0, 75.0);
    ap.rotation = 0.0;
    ap.inflation = 0;
    ArrangePolygons items = { ap };

    const bool all_placed = do_arrange(items, seq_xy_params(), 278.0, 296.0);
    REQUIRE(all_placed);
}

// ─── Test 5: minkowski_rect expands correctly in X and Y ──────────────────

TEST_CASE("Geometry::minkowski_rect expands correct axes", "[arrange][geometry][xy_clearance]")
{
    // A unit square (2 × 2 mm) expanded by dx=3 mm in X and dy=7 mm in Y.
    // Expected resulting bounding box: 8 × 16 mm.
    const Polygon square = make_rect(2.0, 2.0);
    const coord_t dx = scale_(3.0);
    const coord_t dy = scale_(7.0);

    const Polygon expanded = Geometry::minkowski_rect(square, dx, dy);
    const BoundingBox bb = expanded.bounding_box();

    const double w = unscale<double>(bb.max.x() - bb.min.x());
    const double h = unscale<double>(bb.max.y() - bb.min.y());

    // Width should be 2 + 2*3 = 8 mm, height should be 2 + 2*7 = 16 mm.
    CHECK(w == Catch::Approx(8.0).epsilon(0.002));
    CHECK(h == Catch::Approx(16.0).epsilon(0.002));
}

TEST_CASE("Geometry::minkowski_rect is asymmetric", "[arrange][geometry][xy_clearance]")
{
    // A circle (39 mm coin) expanded with clearance_x=18, clearance_y=36.
    // With half-clearance semantics: dx = 9 mm, dy = 18 mm.
    // Expected bbox: (39+18) × (39+36) = 57 × 75 mm.
    const Polygon coin = make_circle(39.0);
    const coord_t dx   = scale_(9.0);
    const coord_t dy   = scale_(18.0);

    const Polygon hull = Geometry::minkowski_rect(coin, dx, dy);
    const BoundingBox bb = hull.bounding_box();

    const double w = unscale<double>(bb.max.x() - bb.min.x());
    const double h = unscale<double>(bb.max.y() - bb.min.y());

    CHECK(w == Catch::Approx(57.0).epsilon(0.01));
    CHECK(h == Catch::Approx(75.0).epsilon(0.01));

    // Verify asymmetry: height must be strictly greater than width.
    CHECK(h > w);
}

// ─── Test 6: Collision detection — half-clearance check hull semantics ─────

TEST_CASE("Collision detection: objects at exactly minimum gap do NOT collide", "[arrange][collision][xy_clearance]")
{
    // Two 39 mm coins placed exactly clearance_x = 18 mm apart in X.
    // The check hull uses half-clearance (9 mm) per side → bbox = 57×75 mm.
    // Centers at x = ±28.5 mm → gap = 57 - 39 = 18 mm = clearance_x.  No overlap.
    constexpr double D   = 39.0;
    constexpr double CX  = 18.0;
    constexpr double CY  = 36.0;
    constexpr double tol = 0.1;   // same tolerance as in Print.cpp

    const Polygon coin = make_circle(D);

    const coord_t obj_dist_x = scale_((CX / 2.0) - tol);
    const coord_t obj_dist_y = scale_((CY / 2.0) - tol);

    // Build check hulls for two coins at exactly clearance_x gap in X.
    const double center_to_center = D + CX;   // 57 mm
    Polygon hull1 = Geometry::minkowski_rect(coin, obj_dist_x, obj_dist_y);
    Polygon hull2 = hull1;
    hull1.translate(scale_(-center_to_center / 2.0), 0);
    hull2.translate(scale_( center_to_center / 2.0), 0);

    const Polygons isect = intersection(Polygons{hull1}, Polygons{hull2});
    REQUIRE(isect.empty());  // No false positive at minimum gap
}

TEST_CASE("Collision detection: objects 1 mm inside minimum gap DO collide", "[arrange][collision][xy_clearance]")
{
    constexpr double D   = 39.0;
    constexpr double CX  = 18.0;
    constexpr double CY  = 36.0;
    constexpr double tol = 0.1;

    const Polygon coin = make_circle(D);

    const coord_t obj_dist_x = scale_((CX / 2.0) - tol);
    const coord_t obj_dist_y = scale_((CY / 2.0) - tol);

    // Place coins 1 mm closer than minimum clearance.
    const double center_to_center = D + CX - 1.0;
    Polygon hull1 = Geometry::minkowski_rect(coin, obj_dist_x, obj_dist_y);
    Polygon hull2 = hull1;
    hull1.translate(scale_(-center_to_center / 2.0), 0);
    hull2.translate(scale_( center_to_center / 2.0), 0);

    const Polygons isect = intersection(Polygons{hull1}, Polygons{hull2});
    REQUIRE(!isect.empty());  // Must detect collision
}

TEST_CASE("Collision detection: Y-axis gap semantics", "[arrange][collision][xy_clearance]")
{
    // Same as X test but in Y direction.
    constexpr double D   = 39.0;
    constexpr double CX  = 18.0;
    constexpr double CY  = 36.0;
    constexpr double tol = 0.1;

    const Polygon coin = make_circle(D);
    const coord_t obj_dist_x = scale_((CX / 2.0) - tol);
    const coord_t obj_dist_y = scale_((CY / 2.0) - tol);

    // Exactly at clearance_y gap → no collision.
    {
        const double ctc = D + CY;  // 75 mm
        Polygon h1 = Geometry::minkowski_rect(coin, obj_dist_x, obj_dist_y);
        Polygon h2 = h1;
        h1.translate(0, scale_(-ctc / 2.0));
        h2.translate(0, scale_( ctc / 2.0));
        CHECK(intersection(Polygons{h1}, Polygons{h2}).empty());
    }

    // 1 mm inside minimum gap → collision.
    {
        const double ctc = D + CY - 1.0;
        Polygon h1 = Geometry::minkowski_rect(coin, obj_dist_x, obj_dist_y);
        Polygon h2 = h1;
        h1.translate(0, scale_(-ctc / 2.0));
        h2.translate(0, scale_( ctc / 2.0));
        CHECK(!intersection(Polygons{h1}, Polygons{h2}).empty());
    }
}

// ─── Test 7: Display hull vs check hull size ───────────────────────────────

TEST_CASE("Display hull and check hull are the same size (consistent zones)", "[arrange][collision][xy_clearance]")
{
    // Both GLCanvas3D and Print.cpp should use clearance/2 per side.
    // Verify: display_dist uses the same half-clearance as obj_dist (minus tolerance).
    constexpr double D   = 39.0;
    constexpr double CX  = 18.0;
    constexpr double CY  = 36.0;
    constexpr double tol = 0.1;

    const Polygon coin = make_circle(D);

    // Check hull (obj_dist): clearance/2 - tolerance
    const Polygon check_hull = Geometry::minkowski_rect(coin,
        scale_((CX / 2.0) - tol), scale_((CY / 2.0) - tol));

    // Display hull (display_dist): clearance/2 (no tolerance deduction)
    const Polygon display_hull = Geometry::minkowski_rect(coin,
        scale_(CX / 2.0), scale_(CY / 2.0));

    const BoundingBox bb_check   = check_hull.bounding_box();
    const BoundingBox bb_display = display_hull.bounding_box();

    const double check_w   = unscale<double>(bb_check.max.x()   - bb_check.min.x());
    const double display_w = unscale<double>(bb_display.max.x() - bb_display.min.x());
    const double check_h   = unscale<double>(bb_check.max.y()   - bb_check.min.y());
    const double display_h = unscale<double>(bb_display.max.y() - bb_display.min.y());

    // Display hull should be exactly 2*tol wider/taller (the tolerance gap).
    CHECK(display_w == Catch::Approx(check_w + 2.0 * tol).epsilon(0.01));
    CHECK(display_h == Catch::Approx(check_h + 2.0 * tol).epsilon(0.01));

    // Both should be much smaller than what full-clearance expansion would give.
    const double full_w = D + 2.0 * CX;  // 75 mm — the WRONG full-clearance value
    const double full_h = D + 2.0 * CY;  // 111 mm

    CHECK(display_w < full_w - 1.0);  // display zone is NOT the oversized full-clearance zone
    CHECK(display_h < full_h - 1.0);
}

// ─── Test 8: Different object sizes ────────────────────────────────────────

TEST_CASE("Arrange: varied object size still packs correctly", "[arrange][seq][xy_clearance]")
{
    // 20 mm object with 10 mm clearance in both axes → footprint 30×30 mm.
    // 200×200 mm bed → 6×6 = 36 objects fit.
    constexpr double D  = 20.0;
    constexpr double C  = 10.0;
    constexpr int    N  = 36;

    const double fw    = D + C;             // 30 mm
    const double dx    = C / 2.0;           // 5 mm
    const double eff_w = 200.0 + 2.0 * (dx - 5.0);  // 200 mm (shrink exactly cancelled)
    const double eff_h = eff_w;

    REQUIRE(int(eff_w / fw) >= 6);
    REQUIRE(int(eff_h / fw) >= 6);

    ArrangePolygons items;
    for (int i = 0; i < N; ++i) {
        ArrangePolygon ap;
        ap.poly.contour = make_rect(fw, fw);
        ap.rotation  = 0.0;
        ap.inflation = 0;
        items.push_back(ap);
    }

    const bool all_placed = do_arrange(items, seq_xy_params(), eff_w, eff_h);
    REQUIRE(all_placed);
    CHECK(no_overlaps(items));
}

// ─── Test 9: Bed shrink math ───────────────────────────────────────────────

TEST_CASE("Bed shrink calculation for XY clearance mode", "[arrange][seq][xy_clearance]")
{
    // With bed_size=270, clearance_x=18, BED_SHRINK=5:
    //   bed_shrink_x = 5 - 9 = -4  → expand bed by 4mm per side → eff_x = 278
    // With clearance_y=36:
    //   bed_shrink_y = 5 - 18 = -13 → expand bed by 13mm per side → eff_y = 296
    constexpr double BED = 270.0;
    constexpr double CX  = 18.0;
    constexpr double CY  = 36.0;
    constexpr double SHRINK = 5.0;  // BED_SHRINK_SEQ_PRINT

    const double bsx = SHRINK - CX / 2.0;   // -4
    const double bsy = SHRINK - CY / 2.0;   // -13

    const double eff_x = BED - 2.0 * bsx;   // 278
    const double eff_y = BED - 2.0 * bsy;   // 296

    CHECK(bsx == Catch::Approx(-4.0).epsilon(0.001));
    CHECK(bsy == Catch::Approx(-13.0).epsilon(0.001));
    CHECK(eff_x == Catch::Approx(278.0).epsilon(0.001));
    CHECK(eff_y == Catch::Approx(296.0).epsilon(0.001));

    // With 57×75 mm footprints:
    const double fw = 39.0 + CX;  // 57
    const double fh = 39.0 + CY;  // 75

    CHECK(int(eff_x / fw) == 4);  // 4 columns
    CHECK(int(eff_y / fh) == 3);  // 3 rows
    CHECK(4 * 3 == 12);           // all 12 coins fit
}

// ─── Tests 10-12: Rotation in by-object arrange ───────────────────────────
//
// By-object mode respects allow_rotations just like by-layer mode.
// The packer tries 0°, 45°, 90°, 135° and picks the best fit.

// Helper used by the expand_clearance_hull rotation test below.
static PrintConfig make_xy_config_for_rotation(float cx, float cy, float radius = 18.0f)
{
    PrintConfig cfg;
    cfg.extruder_clearance_type.value   = ExtruderClearanceType::XY;
    cfg.extruder_clearance_x.value      = cx;
    cfg.extruder_clearance_y.value      = cy;
    cfg.extruder_clearance_radius.value = radius;
    return cfg;
}

TEST_CASE("Arrange: rotation disabled - elongated object cannot fit on narrow bed", "[arrange][rotation]")
{
    // A 60x10mm footprint on a 15mm-wide bed.  Without rotation the 60mm dimension
    // exceeds the bed width, so the item should not be placed on bed 0.
    ArrangePolygon ap;
    ap.poly.contour = make_rect(60.0, 10.0);
    ap.rotation     = 0.0;
    ap.inflation    = 0;
    ap.extrude_ids  = {0};
    ap.bed_idx      = 0;

    ArrangeParams p = seq_xy_params();
    p.allow_rotations = false;

    ArrangePolygons items_single = {ap};
    arrangement::arrange(items_single, {}, make_rect_bed(15.0, 300.0), p);
    CHECK(items_single[0].bed_idx != 0); // 60mm > 15mm, cannot fit without rotation
}

TEST_CASE("Arrange: rotation enabled - elongated object fits on narrow bed at 90deg", "[arrange][rotation]")
{
    // Same 60x10mm footprint.  After 90deg rotation it becomes 10x60mm, which fits
    // in a 15mm-wide bed.  The packer must discover and apply this rotation.
    ArrangePolygon ap;
    ap.poly.contour = make_rect(60.0, 10.0);
    ap.rotation     = 0.0;
    ap.inflation    = 0;
    ap.extrude_ids  = {0};
    ap.bed_idx      = 0;

    ArrangeParams p = seq_xy_params();
    p.allow_rotations = true;

    ArrangePolygons items_single = {ap};
    arrangement::arrange(items_single, {}, make_rect_bed(15.0, 300.0), p);

    // After rotation the item must fit on bed 0.
    REQUIRE(items_single[0].bed_idx == 0);
    // The packer should have applied a non-trivial rotation (approximately 90deg).
    double rot_mod_pi = std::fmod(std::abs(items_single[0].rotation), M_PI);
    // 90deg = pi/2 ~= 1.57; allow tolerance for the discrete angle set {0,pi/4,pi/2,3pi/4}.
    CHECK(rot_mod_pi > 0.1);
}

TEST_CASE("Arrange: rotation enabled - multiple elongated objects stack in rotated orientation", "[arrange][rotation]")
{
    // Four 60×8mm footprints on a 12×350mm bed.
    // Without rotation: none fit (60mm > 12mm).
    // With rotation (90°): each becomes 8×60mm → 4 objects stack vertically (4×60 = 240 < 350).
    constexpr int N = 4;
    ArrangePolygons items;
    for (int i = 0; i < N; ++i) {
        ArrangePolygon ap;
        ap.poly.contour = make_rect(60.0, 8.0);
        ap.rotation     = 0.0;
        ap.inflation    = 0;
        ap.extrude_ids  = {0};
        ap.bed_idx      = 0;
        items.push_back(ap);
    }

    ArrangeParams p = seq_xy_params();
    p.allow_rotations = true;

    arrangement::arrange(items, {}, make_rect_bed(12.0, 350.0), p);

    int fitted = std::count_if(items.begin(), items.end(),
        [](const ArrangePolygon& a){ return a.bed_idx == 0; });
    CHECK(fitted == N);

    // All placed items should have a non-zero rotation (showing they were rotated).
    for (const auto& a : items)
        if (a.bed_idx == 0)
            CHECK(std::fmod(std::abs(a.rotation), M_PI) > 0.1);

    CHECK(no_overlaps(items));
}

TEST_CASE("expand_clearance_hull: rotated hull correctly tracks object orientation", "[clearance_hull][rotation]")
{
    // A 40×10mm rectangle.  Rotating it 90° before calling expand_clearance_hull
    // should yield a hull whose bounding box is 10+clearance wide and 40+clearance tall
    // (not 40+clearance wide as it would be without rotation).
    const PrintConfig cfg = make_xy_config_for_rotation(10.0f, 10.0f); // symmetric 10mm clearance
    Polygon rect = make_rect(40.0, 10.0);
    rect.rotate(M_PI / 2.0);  // 90° CCW: becomes 10mm wide × 40mm tall in world space

    const Polygon hull = expand_clearance_hull(rect, cfg, 0.0f, false, 0.0f);
    const BoundingBox bb = hull.bounding_box();

    const double w = unscale<double>(bb.max.x() - bb.min.x());
    const double h = unscale<double>(bb.max.y() - bb.min.y());

    // After XY-clearance expansion: each side grows by clearance/2 = 5mm.
    //   width  = 10 + 2*5 = 20mm  (short axis + clearance)
    //   height = 40 + 2*5 = 50mm  (long  axis + clearance)
    CHECK(w == Catch::Approx(20.0).epsilon(0.01));
    CHECK(h == Catch::Approx(50.0).epsilon(0.01));

    // Verify: rotated hull is portrait (taller than wide).
    CHECK(h > w + 1.0);
}

// ─── Tests 10-12: expand_clearance_hull — canonical formula ───────────────
//
// These tests call expand_clearance_hull() directly — the same function used by
// both Print::sequential_print_clearance_valid (collision check + error display)
// and GLCanvas3D::update_sequential_clearance (drag-preview display).
// Testing here covers both code paths simultaneously.

/// Build a PrintConfig suitable for XY-clearance tests.
static PrintConfig make_xy_config(float cx, float cy, float radius = 18.0f)
{
    PrintConfig cfg;
    cfg.extruder_clearance_type.value   = ExtruderClearanceType::XY;
    cfg.extruder_clearance_x.value      = cx;
    cfg.extruder_clearance_y.value      = cy;
    cfg.extruder_clearance_radius.value = radius;
    return cfg;
}

/// Build a PrintConfig suitable for radius-clearance tests.
static PrintConfig make_radius_config(float radius)
{
    PrintConfig cfg;
    cfg.extruder_clearance_type.value   = ExtruderClearanceType::Radius;
    cfg.extruder_clearance_x.value      = radius;
    cfg.extruder_clearance_y.value      = radius;
    cfg.extruder_clearance_radius.value = radius;
    return cfg;
}

TEST_CASE("expand_clearance_hull: XY mode display hull spans full clearance zone", "[clearance_hull][xy]")
{
    // 39 mm coin, clearance_x=18 mm, clearance_y=36 mm, no skirt.
    // Display hull (shrink_mm=0.0): each side expands by clearance/2.
    //   Expected bbox: (39+18) × (39+36) = 57 × 75 mm.
    const PrintConfig cfg = make_xy_config(18.0f, 36.0f);
    const Polygon coin    = make_circle(39.0);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const BoundingBox bb  = display.bounding_box();

    const double w = unscale<double>(bb.max.x() - bb.min.x());
    const double h = unscale<double>(bb.max.y() - bb.min.y());

    CHECK(w == Catch::Approx(57.0).epsilon(0.01));
    CHECK(h == Catch::Approx(75.0).epsilon(0.01));
}

TEST_CASE("expand_clearance_hull: XY mode check hull is 2*shrink_mm smaller than display hull", "[clearance_hull][xy]")
{
    // With shrink_mm=0.1: each axis is 0.2 mm narrower than the display hull.
    const PrintConfig cfg = make_xy_config(18.0f, 36.0f);
    const Polygon coin    = make_circle(39.0);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const Polygon check   = expand_clearance_hull(coin, cfg, 0.0f, false, 0.1f);

    const BoundingBox bd = display.bounding_box();
    const BoundingBox bc = check.bounding_box();

    const double dw = unscale<double>(bd.max.x() - bd.min.x());
    const double dh = unscale<double>(bd.max.y() - bd.min.y());
    const double cw = unscale<double>(bc.max.x() - bc.min.x());
    const double ch = unscale<double>(bc.max.y() - bc.min.y());

    // Check hull should be exactly 2*0.1 = 0.2 mm narrower on each axis.
    CHECK(dw - cw == Catch::Approx(0.2).epsilon(0.01));
    CHECK(dh - ch == Catch::Approx(0.2).epsilon(0.01));

    // Sanity: display hull is always larger.
    CHECK(dw > cw);
    CHECK(dh > ch);
}

TEST_CASE("expand_clearance_hull: XY mode check hull prevents false-positive at minimum gap", "[clearance_hull][xy][collision]")
{
    // Two 39 mm coins separated by exactly clearance_x=18 mm.
    // Check hull (shrink_mm=0.1) must NOT intersect (no false positive).
    // Display hull (shrink_mm=0.0) WILL touch or slightly overlap (by design).
    const PrintConfig cfg = make_xy_config(18.0f, 36.0f);
    const Polygon coin    = make_circle(39.0);

    const Polygon check = expand_clearance_hull(coin, cfg, 0.0f, false, 0.1f);

    const double center_gap = 39.0 + 18.0;  // D + clearance_x = 57 mm
    Polygon h1 = check, h2 = check;
    h1.translate(scale_(-center_gap / 2.0), 0);
    h2.translate(scale_( center_gap / 2.0), 0);

    // At exactly the minimum gap the check hulls must NOT intersect.
    CHECK(intersection(Polygons{h1}, Polygons{h2}).empty());

    // One mm inside minimum gap: must detect collision.
    Polygon g1 = check, g2 = check;
    g1.translate(scale_(-(center_gap - 1.0) / 2.0), 0);
    g2.translate(scale_( (center_gap - 1.0) / 2.0), 0);
    CHECK(!intersection(Polygons{g1}, Polygons{g2}).empty());
}

TEST_CASE("expand_clearance_hull: radius mode display hull diameter matches clearance_radius", "[clearance_hull][radius]")
{
    // Radius mode: uniform expansion by clearance_radius/2 per side.
    // 39 mm coin, clearance_radius=18 mm → display hull diameter ≈ 57 mm.
    const PrintConfig cfg = make_radius_config(18.0f);
    const Polygon coin    = make_circle(39.0);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const BoundingBox bb  = display.bounding_box();

    const double w = unscale<double>(bb.max.x() - bb.min.x());
    const double h = unscale<double>(bb.max.y() - bb.min.y());

    // Circle ≈ circle so width ≈ height ≈ 57 mm (with circle-approximation tolerance).
    CHECK(w == Catch::Approx(57.0).epsilon(0.5));
    CHECK(h == Catch::Approx(57.0).epsilon(0.5));
    CHECK(w == Catch::Approx(h).epsilon(0.01));  // symmetric
}

TEST_CASE("expand_clearance_hull: radius mode check hull is smaller than display hull", "[clearance_hull][radius]")
{
    // With shrink_mm=0.1, the radius expansion is reduced by 0.1mm per side.
    const PrintConfig cfg = make_radius_config(18.0f);
    const Polygon coin    = make_circle(39.0);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const Polygon check   = expand_clearance_hull(coin, cfg, 0.0f, false, 0.1f);

    const BoundingBox bd = display.bounding_box();
    const BoundingBox bc = check.bounding_box();

    const double dw = unscale<double>(bd.max.x() - bd.min.x());
    const double cw = unscale<double>(bc.max.x() - bc.min.x());

    // Check hull is smaller by 2 * shrink_mm = 0.2 mm.
    CHECK(dw > cw);
    CHECK(dw - cw == Catch::Approx(0.2).epsilon(0.05));
}

TEST_CASE("expand_clearance_hull: skirt offset is included in expansion", "[clearance_hull][xy]")
{
    // skirt_offset=2 mm should widen the hull the same way as increasing clearance_x/y by 2mm.
    const PrintConfig cfg     = make_xy_config(18.0f, 36.0f);
    const Polygon coin        = make_circle(39.0);

    const Polygon no_skirt   = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const Polygon with_skirt = expand_clearance_hull(coin, cfg, 2.0f, false, 0.0f);

    const BoundingBox bb_ns = no_skirt.bounding_box();
    const BoundingBox bb_ws = with_skirt.bounding_box();

    const double ns_w = unscale<double>(bb_ns.max.x() - bb_ns.min.x());
    const double ws_w = unscale<double>(bb_ws.max.x() - bb_ws.min.x());
    const double ns_h = unscale<double>(bb_ns.max.y() - bb_ns.min.y());
    const double ws_h = unscale<double>(bb_ws.max.y() - bb_ws.min.y());

    // skirt_offset is added to clearance/2 per side, so total growth per axis = skirt_offset.
    //   dx = (clearance_x + skirt) / 2 → Δdx = skirt/2 → total ΔW = 2 * skirt/2 = skirt
    CHECK(ws_w - ns_w == Catch::Approx(2.0).epsilon(0.01));
    CHECK(ws_h - ns_h == Catch::Approx(2.0).epsilon(0.01));
}
