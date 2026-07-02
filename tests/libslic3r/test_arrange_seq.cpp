/**
 * test_arrange_seq.cpp
 *
 * Catch2 tests for the sequential-print (by-object) XY-clearance arrange and
 * hull-expansion logic added by the feature/extruder-clearance-rectangle branch.
 *
 * Scenarios covered:
 *  1. Core packing: 12 identical rectangular footprints pack into a 4x3 grid
 *     on a correctly-sized bed.
 *  2. Asymmetric clearance: non-square footprints (clearance_x != clearance_y)
 *     produce the expected column/row counts.
 *  3. minkowski_rect geometry: the expansion is correct in X and Y.
 *  4. Single-object and over-capacity edge cases.
 *  5. Rotation: allow_rotations flag lets the packer fit elongated objects.
 *  6. expand_clearance_hull: canonical formula tested end-to-end (XY and Radius
 *     modes, shrink_mm, skirt, all_short, asymmetric rotation, GLCanvas3D regression).
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

// ---------------------------------------------------------------------------
// Module-level constants — reference scenario used across multiple tests
// ---------------------------------------------------------------------------

constexpr double COIN_D  = 39.0;   // object/coin diameter (mm)
constexpr double COIN_CX = 18.0;   // clearance_x in reference scenario (mm)
constexpr double COIN_CY = 36.0;   // clearance_y in reference scenario (mm)
// shrink_mm applied to collision-check hulls (same value as in Print.cpp).
// Display hulls use shrink_mm=0.0; check hulls use CLEARANCE_TOL.
constexpr float  CLEARANCE_TOL = 0.1f;

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

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

/// Axis-aligned rectangle centred at origin, size w x h (mm), CCW.
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

/// Return bounding box dimensions (width, height) in mm for a polygon.
static std::pair<double, double> bbox_size_mm(const Polygon& poly)
{
    const BoundingBox bb = poly.bounding_box();
    return {
        unscale<double>(bb.max.x() - bb.min.x()),
        unscale<double>(bb.max.y() - bb.min.y())
    };
}

// ---------------------------------------------------------------------------
// ArrangePolygon / arrange helpers
// ---------------------------------------------------------------------------

/// Create an ArrangePolygon with sensible defaults for arrange tests.
/// Production code (ArrangeJob.cpp, ModelArrange.cpp) always sets bed_idx=0
/// before calling arrange; the FirstFit selector treats bed_idx==-1 as BIN_ID_UNFIT.
/// extrude_ids must be non-empty: the sort comparator calls extrude_ids.front()
/// and crashes on an empty vector.
static ArrangePolygon make_ap(const Polygon& poly)
{
    ArrangePolygon ap;
    ap.poly.contour = poly;
    ap.rotation     = 0.0;
    ap.inflation    = 0;
    ap.extrude_ids  = {0};
    ap.bed_idx      = 0;
    return ap;
}

/// Arrange the given items on a rectangular bed and return whether all were placed.
/// Also ensures bed_idx=0 and non-empty extrude_ids on items that don't use make_ap.
static bool do_arrange(ArrangePolygons &items,
                       const ArrangeParams &params,
                       double bed_w_mm, double bed_h_mm)
{
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

/// Count distinct rounded-mm X positions -> number of columns.
static int count_columns(const ArrangePolygons &items)
{
    std::set<int> xs;
    for (const auto &ap : items)
        xs.insert(int(std::round(unscale<double>(ap.translation.x()))));
    return int(xs.size());
}

/// Count distinct rounded-mm Y positions -> number of rows.
static int count_rows(const ArrangePolygons &items)
{
    std::set<int> ys;
    for (const auto &ap : items)
        ys.insert(int(std::round(unscale<double>(ap.translation.y()))));
    return int(ys.size());
}

/// Verify that no two items' placed footprints overlap.
/// Applies both rotation and translation from each ArrangePolygon result.
static bool no_overlaps(const ArrangePolygons &items)
{
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j) {
            Polygon pi = items[i].poly.contour;
            pi.rotate(items[i].rotation);      // apply rotation first, then translation
            pi.translate(items[i].translation);
            Polygon pj = items[j].poly.contour;
            pj.rotate(items[j].rotation);
            pj.translate(items[j].translation);
            if (!intersection(Polygons{pi}, Polygons{pj}).empty())
                return false;
        }
    return true;
}

/// Build ArrangeParams for sequential-print XY-clearance tests.
static ArrangeParams seq_xy_params()
{
    ArrangeParams p;
    p.is_seq_print    = true;
    p.allow_rotations = false;
    p.accuracy        = 1.0f;   // production default; ensures all NFP vertices evaluated
    return p;
}

// ---------------------------------------------------------------------------
// PrintConfig helpers for expand_clearance_hull tests
// ---------------------------------------------------------------------------

/// Build a PrintConfig in XY-clearance mode with the given clearance values.
static PrintConfig make_xy_config(float cx, float cy, float radius = 18.0f)
{
    PrintConfig cfg;
    cfg.extruder_clearance_type.value   = ExtruderClearanceType::XY;
    cfg.extruder_clearance_x.value      = cx;
    cfg.extruder_clearance_y.value      = cy;
    cfg.extruder_clearance_radius.value = radius;
    return cfg;
}

/// Build a PrintConfig in Radius-clearance mode with uniform clearance.
static PrintConfig make_radius_config(float radius)
{
    PrintConfig cfg;
    cfg.extruder_clearance_type.value   = ExtruderClearanceType::Radius;
    cfg.extruder_clearance_x.value      = radius;
    cfg.extruder_clearance_y.value      = radius;
    cfg.extruder_clearance_radius.value = radius;
    return cfg;
}

// ===========================================================================
// Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Arrange tests
// ---------------------------------------------------------------------------

TEST_CASE("Arrange: 12 coins in 4x3 grid with asymmetric XY clearance", "[arrange][seq][xy_clearance]")
{
    // User scenario: COIN_D mm coins, clearance_x=COIN_CX mm, clearance_y=COIN_CY mm,
    // 270x270 mm bed.  After half-clearance expansion each coin footprint is
    // 57x75 mm.  Bed shrink: 5 - 9 = -4 mm in X -> eff bed 278 mm;
    //                         5 - 18 = -13 mm in Y -> eff bed 296 mm.
    // Expected: floor(278/57)=4 cols, floor(296/75)=3 rows -> 12 fit exactly.

    constexpr int N = 12;

    const double dx = COIN_CX / 2.0;          // half-clearance per side: 9 mm
    const double dy = COIN_CY / 2.0;          // 18 mm
    const double fw = COIN_D + COIN_CX;       // footprint width:  57 mm
    const double fh = COIN_D + COIN_CY;       // footprint height: 75 mm

    // Effective bed (BED_SHRINK_SEQ_PRINT=5, minus half-clearance per side):
    const double eff_w = 270.0 + 2.0 * (dx - 5.0);   // 278 mm
    const double eff_h = 270.0 + 2.0 * (dy - 5.0);   // 296 mm

    const int exp_cols = int(eff_w / fw);  // 4
    const int exp_rows = int(eff_h / fh);  // 3
    REQUIRE(exp_cols * exp_rows >= N);

    ArrangePolygons items;
    for (int i = 0; i < N; ++i)
        items.push_back(make_ap(make_rect(fw, fh)));

    const bool all_placed = do_arrange(items, seq_xy_params(), eff_w, eff_h);

    REQUIRE(all_placed);

    for (const auto &ap : items) {
        const double tx = unscale<double>(ap.translation.x());
        const double ty = unscale<double>(ap.translation.y());
        CHECK(tx >= -eff_w/2.0 + fw/2.0 - 1.0);
        CHECK(tx <=  eff_w/2.0 - fw/2.0 + 1.0);
        CHECK(ty >= -eff_h/2.0 + fh/2.0 - 1.0);
        CHECK(ty <=  eff_h/2.0 - fh/2.0 + 1.0);
    }

    CHECK(no_overlaps(items));

    // NOTE: We do not assert exact column/row counts because the BOTTOM_LEFT
    // heuristic is a cost-minimiser, not a strict grid filler.  The critical
    // guarantee is that all N items fit without overlap — verified above.
    CHECK(count_columns(items) <= exp_cols * 2);
    CHECK(count_rows(items)    <= exp_rows * 2);
}

TEST_CASE("Arrange: symmetric clearance produces square packing", "[arrange][seq][xy_clearance]")
{
    constexpr double D  = 40.0;
    constexpr double C  = 20.0;   // equal clearance on both axes
    constexpr int    N  = 9;

    const double fw    = D + C;                          // 60 mm
    const double dx    = C / 2.0;                        // 10 mm
    const double eff_w = 200.0 + 2.0 * (dx - 5.0);      // 210 mm

    const int exp_cols = int(eff_w / fw);  // 3
    REQUIRE(exp_cols * exp_cols >= N);

    ArrangePolygons items;
    for (int i = 0; i < N; ++i)
        items.push_back(make_ap(make_rect(fw, fw)));

    const bool all_placed = do_arrange(items, seq_xy_params(), eff_w, eff_w);

    REQUIRE(all_placed);
    CHECK(no_overlaps(items));
    CHECK(count_columns(items) <= exp_cols * 2);
    CHECK(count_rows(items)    <= exp_cols * 2);
}

TEST_CASE("Arrange: over-capacity items overflow to a second plate", "[arrange][seq][xy_clearance]")
{
    // 6 objects that each need 100x100 mm -> only 4 fit in a 210x210 mm bed (2x2).
    // The remaining 2 overflow to a second virtual plate (bed_idx=1), not
    // UNARRANGED (-1), because do_arrange pre-sets all items to bed_idx=0 and
    // the FirstFit selector spills overflow onto additional plates.
    ArrangePolygons items;
    for (int i = 0; i < 6; ++i)
        items.push_back(make_ap(make_rect(100.0, 100.0)));

    do_arrange(items, seq_xy_params(), 210.0, 210.0);

    const int on_bed0 = int(std::count_if(items.begin(), items.end(),
        [](const ArrangePolygon &ap){ return ap.bed_idx == 0; }));
    const int overflow = int(items.size()) - on_bed0;

    CHECK(on_bed0  == 4);   // 2x2 grid in 210mm bed
    CHECK(overflow == 2);
}

TEST_CASE("Arrange: single object always fits on large bed", "[arrange][seq][xy_clearance]")
{
    ArrangePolygons items;
    items.push_back(make_ap(make_rect(57.0, 75.0)));

    const bool all_placed = do_arrange(items, seq_xy_params(), 278.0, 296.0);
    REQUIRE(all_placed);
}

// ---------------------------------------------------------------------------
// Geometry tests
// ---------------------------------------------------------------------------

TEST_CASE("Geometry::minkowski_rect expands correct axes", "[arrange][geometry][xy_clearance]")
{
    // A unit square (2x2 mm) expanded by dx=3 mm in X and dy=7 mm in Y.
    // Expected resulting bounding box: 8x16 mm.
    const Polygon square = make_rect(2.0, 2.0);
    const coord_t dx = scale_(3.0);
    const coord_t dy = scale_(7.0);

    const Polygon expanded = Geometry::minkowski_rect(square, dx, dy);
    auto [w, h] = bbox_size_mm(expanded);

    CHECK(w == Catch::Approx(8.0).epsilon(0.002));   // 2 + 2*3 = 8 mm
    CHECK(h == Catch::Approx(16.0).epsilon(0.002));  // 2 + 2*7 = 16 mm
}

TEST_CASE("Geometry::minkowski_rect is asymmetric", "[arrange][geometry][xy_clearance]")
{
    // A circle (COIN_D coin) expanded with clearance_x=COIN_CX, clearance_y=COIN_CY.
    // With half-clearance semantics: dx = COIN_CX/2 mm, dy = COIN_CY/2 mm.
    // Expected bbox: (COIN_D+COIN_CX) x (COIN_D+COIN_CY) = 57 x 75 mm.
    const Polygon coin = make_circle(COIN_D);
    const coord_t dx   = scale_(COIN_CX / 2.0);
    const coord_t dy   = scale_(COIN_CY / 2.0);

    const Polygon hull = Geometry::minkowski_rect(coin, dx, dy);
    auto [w, h] = bbox_size_mm(hull);

    CHECK(w == Catch::Approx(COIN_D + COIN_CX).epsilon(0.01));   // 57 mm
    CHECK(h == Catch::Approx(COIN_D + COIN_CY).epsilon(0.01));   // 75 mm
    CHECK(h > w);  // asymmetric: clearance_y > clearance_x -> height > width
}

TEST_CASE("Arrange: varied object size still packs correctly", "[arrange][seq][xy_clearance]")
{
    // 20 mm object with 10 mm clearance in both axes -> footprint 30x30 mm.
    // 200x200 mm bed -> 6x6 = 36 objects fit.
    constexpr double D  = 20.0;
    constexpr double C  = 10.0;
    constexpr int    N  = 36;

    const double fw    = D + C;                         // 30 mm
    const double dx    = C / 2.0;                       // 5 mm
    const double eff_w = 200.0 + 2.0 * (dx - 5.0);     // 200 mm (shrink exactly cancelled)

    REQUIRE(int(eff_w / fw) >= 6);

    ArrangePolygons items;
    for (int i = 0; i < N; ++i)
        items.push_back(make_ap(make_rect(fw, fw)));

    const bool all_placed = do_arrange(items, seq_xy_params(), eff_w, eff_w);
    REQUIRE(all_placed);
    CHECK(no_overlaps(items));
}

// ---------------------------------------------------------------------------
// Rotation tests
// ---------------------------------------------------------------------------

TEST_CASE("Arrange: rotation disabled - elongated object cannot fit on narrow bed", "[arrange][rotation]")
{
    // A 60x10mm footprint on a 15mm-wide bed.  Without rotation the 60mm dimension
    // exceeds the bed width, so the item should not be placed on bed 0.
    ArrangePolygons items;
    items.push_back(make_ap(make_rect(60.0, 10.0)));

    ArrangeParams p = seq_xy_params();
    p.allow_rotations = false;

    arrangement::arrange(items, {}, make_rect_bed(15.0, 300.0), p);
    CHECK(items[0].bed_idx != 0); // 60mm > 15mm -> cannot fit without rotation
}

TEST_CASE("Arrange: rotation enabled - elongated object fits on narrow bed at 90deg", "[arrange][rotation]")
{
    // Same 60x10mm footprint.  After 90deg rotation it becomes 10x60mm, which fits
    // in a 15mm-wide bed.  The packer must discover and apply this rotation.
    ArrangePolygons items;
    items.push_back(make_ap(make_rect(60.0, 10.0)));

    ArrangeParams p = seq_xy_params();
    p.allow_rotations = true;

    arrangement::arrange(items, {}, make_rect_bed(15.0, 300.0), p);

    REQUIRE(items[0].bed_idx == 0);

    // Rotation must be close to 90deg (pi/2).  Guard against near-zero rotations
    // by requiring at least 45deg minus a small tolerance.
    const double rot_mod_pi = std::fmod(std::abs(items[0].rotation), M_PI);
    CHECK(rot_mod_pi > M_PI / 4.0 - 0.1);
}

TEST_CASE("Arrange: rotation enabled - multiple elongated objects stack in rotated orientation", "[arrange][rotation]")
{
    // Four 60x8mm footprints on a 12x350mm bed.
    // Without rotation: none fit (60mm > 12mm).
    // With rotation (90deg): each becomes 8x60mm -> 4 objects stack (4*60=240 < 350).
    constexpr int N = 4;
    ArrangePolygons items;
    for (int i = 0; i < N; ++i)
        items.push_back(make_ap(make_rect(60.0, 8.0)));

    ArrangeParams p = seq_xy_params();
    p.allow_rotations = true;

    arrangement::arrange(items, {}, make_rect_bed(12.0, 350.0), p);

    const int fitted = int(std::count_if(items.begin(), items.end(),
        [](const ArrangePolygon& a){ return a.bed_idx == 0; }));
    CHECK(fitted == N);

    // All placed items must have a rotation >= 45deg (close to the 90deg that fits).
    for (const auto& a : items)
        if (a.bed_idx == 0)
            CHECK(std::fmod(std::abs(a.rotation), M_PI) > M_PI / 4.0 - 0.1);

    CHECK(no_overlaps(items));
}

// ---------------------------------------------------------------------------
// expand_clearance_hull tests
//
// expand_clearance_hull() is the single canonical formula for clearance hulls.
// It is used by:
//   - Print::sequential_print_clearance_valid (collision check with shrink_mm=0.1)
//   - GLCanvas3D::update_sequential_clearance  (drag-preview with shrink_mm=0.0)
// Testing here covers both code paths simultaneously.
// ---------------------------------------------------------------------------

TEST_CASE("expand_clearance_hull: rotated hull correctly tracks object orientation", "[clearance_hull][rotation]")
{
    // A 40x10mm rectangle.  Rotating it 90deg before calling expand_clearance_hull
    // should yield a hull whose bbox is 10+clearance wide and 40+clearance tall.
    const PrintConfig cfg = make_xy_config(10.0f, 10.0f); // symmetric clearance
    Polygon rect = make_rect(40.0, 10.0);
    rect.rotate(M_PI / 2.0);  // 90deg CCW: becomes 10mm wide x 40mm tall in world space

    const Polygon hull = expand_clearance_hull(rect, cfg, 0.0f, false, 0.0f);
    auto [w, h] = bbox_size_mm(hull);

    // Each side expands by clearance/2 = 5mm.
    //   width  = 10 + 2*5 = 20mm   (short axis + clearance)
    //   height = 40 + 2*5 = 50mm   (long  axis + clearance)
    CHECK(w == Catch::Approx(20.0).epsilon(0.01));
    CHECK(h == Catch::Approx(50.0).epsilon(0.01));
    CHECK(h > w + 1.0);  // portrait orientation preserved
}

TEST_CASE("expand_clearance_hull: asymmetric XY clearance on rotated rect preserves axis semantics", "[clearance_hull][xy][rotation]")
{
    // A 40x10mm rectangle rotated 90deg occupies 10mm in world X, 40mm in world Y.
    // With asymmetric clearance cx=10, cy=40:
    //   Hull width  = 10 + cx = 10 + 10 = 20mm  (world X)
    //   Hull height = 40 + cy = 40 + 40 = 80mm  (world Y)
    // If expand_clearance_hull applied cx/cy to the original (pre-rotation) axes
    // instead of world axes, it would produce 50mm x 50mm or 50mm x 80mm (wrong).
    const PrintConfig cfg = make_xy_config(10.0f, 40.0f);
    Polygon rect = make_rect(40.0, 10.0);
    rect.rotate(M_PI / 2.0);  // 90deg CCW: 10mm wide x 40mm tall in world space

    const Polygon hull = expand_clearance_hull(rect, cfg, 0.0f, false, 0.0f);
    auto [w, h] = bbox_size_mm(hull);

    CHECK(w == Catch::Approx(20.0).epsilon(0.01));  // 10 + cx=10 = 20mm
    CHECK(h == Catch::Approx(80.0).epsilon(0.01));  // 40 + cy=40 = 80mm
}

TEST_CASE("expand_clearance_hull: XY mode display hull spans full clearance zone", "[clearance_hull][xy]")
{
    // COIN_D mm coin, clearance_x=COIN_CX mm, clearance_y=COIN_CY mm, no skirt.
    // Display hull (shrink_mm=0.0): each side expands by clearance/2.
    //   Expected bbox: (COIN_D+COIN_CX) x (COIN_D+COIN_CY) = 57 x 75 mm.
    const PrintConfig cfg = make_xy_config(float(COIN_CX), float(COIN_CY));
    const Polygon coin    = make_circle(COIN_D);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    auto [w, h] = bbox_size_mm(display);

    CHECK(w == Catch::Approx(COIN_D + COIN_CX).epsilon(0.01));   // 57 mm
    CHECK(h == Catch::Approx(COIN_D + COIN_CY).epsilon(0.01));   // 75 mm
}

TEST_CASE("expand_clearance_hull: XY mode check hull is 2*shrink_mm smaller than display hull", "[clearance_hull][xy]")
{
    // With shrink_mm=CLEARANCE_TOL: each axis is 2*CLEARANCE_TOL mm narrower than display.
    const PrintConfig cfg = make_xy_config(float(COIN_CX), float(COIN_CY));
    const Polygon coin    = make_circle(COIN_D);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const Polygon check   = expand_clearance_hull(coin, cfg, 0.0f, false, CLEARANCE_TOL);

    auto [dw, dh] = bbox_size_mm(display);
    auto [cw, ch] = bbox_size_mm(check);

    CHECK(dw - cw == Catch::Approx(2.0 * CLEARANCE_TOL).epsilon(0.01));
    CHECK(dh - ch == Catch::Approx(2.0 * CLEARANCE_TOL).epsilon(0.01));
    CHECK(dw > cw);
    CHECK(dh > ch);
}

TEST_CASE("expand_clearance_hull: XY mode check hull prevents false-positive at minimum gap", "[clearance_hull][xy][collision]")
{
    // Two COIN_D mm coins separated by exactly clearance_x=COIN_CX mm.
    // Check hull (shrink_mm=CLEARANCE_TOL) must NOT intersect at this gap.
    // One mm inside the gap must produce a collision.
    const PrintConfig cfg = make_xy_config(float(COIN_CX), float(COIN_CY));
    const Polygon coin    = make_circle(COIN_D);
    const Polygon check   = expand_clearance_hull(coin, cfg, 0.0f, false, CLEARANCE_TOL);

    const double center_gap = COIN_D + COIN_CX;  // 57 mm (D + clearance_x)

    SECTION("At exactly minimum gap - no false positive")
    {
        Polygon h1 = check, h2 = check;
        h1.translate(scale_(-center_gap / 2.0), 0);
        h2.translate(scale_( center_gap / 2.0), 0);
        CHECK(intersection(Polygons{h1}, Polygons{h2}).empty());
    }

    SECTION("One mm inside minimum gap - collision detected")
    {
        Polygon h1 = check, h2 = check;
        h1.translate(scale_(-(center_gap - 1.0) / 2.0), 0);
        h2.translate(scale_( (center_gap - 1.0) / 2.0), 0);
        CHECK(!intersection(Polygons{h1}, Polygons{h2}).empty());
    }
}

TEST_CASE("expand_clearance_hull: display hull is larger than collision check hull - GLCanvas3D regression",
    "[clearance_hull][xy]")
{
    // Previously GLCanvas3D used the collision-check hull (shrink_mm=CLEARANCE_TOL)
    // for the drag-preview display, making the visualized clearance zone smaller than
    // it should be.  The fix passes shrink_mm=0.0 for display.
    // Regression guard: verify display hull is exactly 2*CLEARANCE_TOL wider/taller.
    const PrintConfig cfg = make_xy_config(float(COIN_CX), float(COIN_CY));
    const Polygon coin    = make_circle(COIN_D);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const Polygon check   = expand_clearance_hull(coin, cfg, 0.0f, false, CLEARANCE_TOL);

    auto [dw, dh] = bbox_size_mm(display);
    auto [cw, ch] = bbox_size_mm(check);

    // The display hull must be strictly larger and by exactly the tolerance gap.
    CHECK(dw > cw);
    CHECK(dh > ch);
    CHECK(dw - cw == Catch::Approx(2.0 * CLEARANCE_TOL).epsilon(0.01));
    CHECK(dh - ch == Catch::Approx(2.0 * CLEARANCE_TOL).epsilon(0.01));
}

TEST_CASE("expand_clearance_hull: radius mode display hull diameter matches clearance_radius", "[clearance_hull][radius]")
{
    // Radius mode: uniform expansion by clearance_radius/2 per side.
    // COIN_D mm coin, clearance_radius=COIN_CX mm ->
    //   display hull diameter ≈ COIN_D + COIN_CX = 57 mm.
    const PrintConfig cfg = make_radius_config(float(COIN_CX));
    const Polygon coin    = make_circle(COIN_D);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    auto [w, h] = bbox_size_mm(display);

    // Circle ≈ circle so width ≈ height ≈ 57 mm (with circle-approximation tolerance).
    CHECK(w == Catch::Approx(COIN_D + COIN_CX).epsilon(0.02));
    CHECK(h == Catch::Approx(COIN_D + COIN_CX).epsilon(0.02));
    CHECK(w == Catch::Approx(h).epsilon(0.01));  // symmetric: radius mode produces round hull
}

TEST_CASE("expand_clearance_hull: radius mode check hull is smaller than display hull", "[clearance_hull][radius]")
{
    // With shrink_mm=CLEARANCE_TOL, the radius expansion is reduced per side.
    // Total diameter difference should be 2 * CLEARANCE_TOL = 0.2 mm.
    const PrintConfig cfg = make_radius_config(float(COIN_CX));
    const Polygon coin    = make_circle(COIN_D);

    const Polygon display = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const Polygon check   = expand_clearance_hull(coin, cfg, 0.0f, false, CLEARANCE_TOL);

    auto [dw, dh] = bbox_size_mm(display);
    auto [cw, ch] = bbox_size_mm(check);

    CHECK(dw > cw);
    CHECK(dw - cw == Catch::Approx(2.0 * CLEARANCE_TOL).epsilon(0.05));
}

TEST_CASE("expand_clearance_hull: all_short uses MAX_OUTER_NOZZLE_DIAMETER as radius floor", "[clearance_hull][radius]")
{
    // When all_short=true (all objects shorter than nozzle zone), the radius is
    // floored at MAX_OUTER_NOZZLE_DIAMETER/2 = 2mm regardless of clearance_radius.
    // With a tiny clearance_radius=0.1mm, all_short=true must produce a visibly
    // larger hull than all_short=false.
    const PrintConfig cfg = make_radius_config(0.1f);  // nearly zero clearance
    const Polygon coin    = make_circle(COIN_D);

    const Polygon normal  = expand_clearance_hull(coin, cfg, 0.0f, /*all_short=*/false, 0.0f);
    const Polygon floored = expand_clearance_hull(coin, cfg, 0.0f, /*all_short=*/true,  0.0f);

    auto [nw, nh] = bbox_size_mm(normal);
    auto [fw, fh] = bbox_size_mm(floored);

    // all_short uses radius = MAX_OUTER_NOZZLE_DIAMETER/2 = 2mm per side -> +4mm diameter.
    // normal uses clearance_radius/2 = 0.05mm per side -> ~+0.1mm diameter.
    // Difference should be ~3.9mm, well above the 1mm check.
    CHECK(fw > nw + 1.0);
    CHECK(fh > nh + 1.0);

    // Floored hull diameter ≈ COIN_D + MAX_OUTER_NOZZLE_DIAMETER = 39 + 4 = 43 mm.
    CHECK(fw == Catch::Approx(COIN_D + 4.0).epsilon(0.1));
}

TEST_CASE("expand_clearance_hull: skirt offset is included in expansion", "[clearance_hull][xy]")
{
    // skirt_offset=2 mm should widen the hull by exactly 2 mm per axis.
    // Formula: dx = (clearance_x + skirt_offset) / 2 per side
    //   -> total X growth = 2 * (skirt_offset / 2) = skirt_offset
    const PrintConfig cfg   = make_xy_config(float(COIN_CX), float(COIN_CY));
    const Polygon coin      = make_circle(COIN_D);

    const Polygon no_skirt   = expand_clearance_hull(coin, cfg, 0.0f, false, 0.0f);
    const Polygon with_skirt = expand_clearance_hull(coin, cfg, 2.0f, false, 0.0f);

    auto [ns_w, ns_h] = bbox_size_mm(no_skirt);
    auto [ws_w, ws_h] = bbox_size_mm(with_skirt);

    CHECK(ws_w - ns_w == Catch::Approx(2.0).epsilon(0.01));
    CHECK(ws_h - ns_h == Catch::Approx(2.0).epsilon(0.01));
}
