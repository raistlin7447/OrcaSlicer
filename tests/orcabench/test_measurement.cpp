#include <catch2/catch_all.hpp>

#include "core/Measurement.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>

using namespace Slic3r::Bench;
using namespace std::chrono_literals;

TEST_CASE("a span keeps what the workload reported", "[OrcaBench][Measurement]")
{
    const Clock::time_point start = Clock::time_point {} + 1s;
    Measurement measurement;
    measurement.span("posSlice", Scope::object(2), start, start + 5ms, {{"peak_rss_bytes", 1024.0}});
    REQUIRE(measurement.timeline().size() == 1);
    const StageSpan& span = measurement.timeline().front();
    CHECK(span.stage == "posSlice");
    CHECK(span.scope == "object:2");
    CHECK(span.started_at == start);
    CHECK(span.done_at == start + 5ms);
    REQUIRE(span.metrics.count("peak_rss_bytes") == 1);
    CHECK_THAT(span.metrics.at("peak_rss_bytes"), Catch::Matchers::WithinAbs(1024.0, 0.0));
}

TEST_CASE("a span that ends before it starts is refused", "[OrcaBench][Measurement]")
{
    const Clock::time_point start = Clock::time_point {} + 1s;
    Measurement measurement;
    CHECK_THROWS_AS(measurement.span("posSlice", Scope::print(), start, start - 1ms), std::invalid_argument);
}

TEST_CASE("a span without a stage is refused", "[OrcaBench][Measurement]")
{
    Measurement measurement;
    CHECK_THROWS_AS(measurement.span("", Scope::print(), Clock::time_point {}, Clock::time_point {}), std::invalid_argument);
}

TEST_CASE("a metric belongs to the iteration even after a span", "[OrcaBench][Measurement]")
{
    const Clock::time_point start = Clock::time_point {} + 1s;
    Measurement measurement;
    measurement.span("posSlice", Scope::print(), start, start + 5ms);
    measurement.metric("cpu_ratio", 7.5);
    REQUIRE(measurement.timeline().size() == 1);
    CHECK(measurement.timeline().front().metrics.empty());
    REQUIRE(measurement.metrics().count("cpu_ratio") == 1);
    CHECK_THAT(measurement.metrics().at("cpu_ratio"), Catch::Matchers::WithinAbs(7.5, 0.0));
}

TEST_CASE("a number that is not finite is refused where it is reported", "[OrcaBench][Measurement]")
{
    const double value = GENERATE(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity());
    CAPTURE(value);

    Measurement span_metric;
    CHECK_THROWS_AS(span_metric.span("posSlice", Scope::print(), Clock::time_point {}, Clock::time_point {}, {{"peak_rss_bytes", value}}),
                    std::invalid_argument);

    Measurement iteration_metric;
    CHECK_THROWS_AS(iteration_metric.metric("cpu_ratio", value), std::invalid_argument);

    WorkStats volume;
    volume.extrusion_mm3 = value;
    Measurement work_volume;
    CHECK_THROWS_AS(work_volume.work(volume), std::invalid_argument);

    WorkStats by_role;
    by_role.metrics["extrusion_mm3.support"] = value;
    Measurement work_metric;
    CHECK_THROWS_AS(work_metric.work(by_role), std::invalid_argument);
}

TEST_CASE("a metric reported twice in one iteration is refused", "[OrcaBench][Measurement]")
{
    Measurement measurement;
    measurement.metric("cpu_ratio", 7.5);
    CHECK_THROWS_AS(measurement.metric("cpu_ratio", 7.5), std::invalid_argument);
}

TEST_CASE("work stats and the output hash are kept", "[OrcaBench][Measurement]")
{
    Measurement measurement;
    WorkStats   work;
    work.layers = 190;
    measurement.work(work);
    measurement.output_hash(0x3bbecdbaa64c8fe8);
    REQUIRE(measurement.work_stats().has_value());
    CHECK(measurement.work_stats()->layers == 190);
    REQUIRE(measurement.hash().has_value());
    CHECK(*measurement.hash() == 0x3bbecdbaa64c8fe8);
}

TEST_CASE("work stats or an output hash reported twice is refused", "[OrcaBench][Measurement]")
{
    Measurement measurement;
    measurement.work(WorkStats {});
    CHECK_THROWS_AS(measurement.work(WorkStats {}), std::invalid_argument);
    measurement.output_hash(1);
    CHECK_THROWS_AS(measurement.output_hash(2), std::invalid_argument);
}

TEST_CASE("a scope names the print or one object", "[OrcaBench][Measurement]")
{
    CHECK(Scope::print().text() == "print");
    CHECK(Scope::object(3).text() == "object:3");
}
