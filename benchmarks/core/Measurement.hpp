#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Slic3r { namespace Bench {

// Where a span happened, either the whole print or one of its objects.
class Scope
{
public:
    static Scope print();
    static Scope object(std::size_t index);

    // "print" or "object:N", the form StageSpan::scope holds.
    const std::string& text() const { return m_text; }

private:
    explicit Scope(std::string text) : m_text(std::move(text)) {}

    std::string m_text;
};

// What one iteration of a workload reports, which the real workloads and the tests' fake both
// write through, so nothing between a workload and its result depends on slicing.
class Measurement
{
public:
    // Throws std::invalid_argument for a span without a stage, one that ends before it starts, or a
    // metric that is not finite.
    void span(std::string stage, const Scope& scope, Clock::time_point started_at, Clock::time_point done_at,
              Metrics metrics = {});

    // A measurement of the whole iteration, beside the wall time, CPU time and memory the Runner
    // takes, refused with std::invalid_argument when it is not finite or its key was reported already.
    void metric(std::string key, double value);

    // Each throws std::invalid_argument when reported twice in one iteration, and work() also for a
    // number that is not finite.
    void work(WorkStats work);
    void output_hash(std::uint64_t hash);

    const Timeline&                     timeline() const { return m_timeline; }
    const Metrics&                      metrics() const { return m_metrics; }
    const std::optional<WorkStats>&     work_stats() const { return m_work; }
    const std::optional<std::uint64_t>& hash() const { return m_hash; }

private:
    Timeline                     m_timeline;
    Metrics                      m_metrics;
    std::optional<WorkStats>     m_work;
    std::optional<std::uint64_t> m_hash;
};

}} // namespace Slic3r::Bench
