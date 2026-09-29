#pragma once

#include "core/Policy.hpp"
#include "core/Result.hpp"
#include "core/Workload.hpp"

#include <vector>

namespace Slic3r { namespace Bench {

// What a run sets up around all its workloads, such as the thread cap, declared here so core
// stays free of the slicer's dependencies.
class RunEnvironment
{
public:
    virtual ~RunEnvironment() = default;

    // Called once before the first workload, where an exception ends the run.
    virtual void enter(const Policy& policy) = 0;

    // Called once after the last workload, and only when enter() returned.
    virtual void leave() noexcept = 0;
};

// Runs each entry the policy admits through setup() once, then prepare() and execute() for every
// warmup and timed pass. A workload that throws, or whose pass differs from its first, fails with
// a reason and the run goes on.
Result run_suite(const std::vector<CatalogEntry>& entries, const Policy& policy, const WorkloadKinds& kinds,
                 RunEnvironment& environment);

}} // namespace Slic3r::Bench
