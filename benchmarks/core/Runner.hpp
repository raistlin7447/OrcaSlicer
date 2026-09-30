#pragma once

#include "core/Policy.hpp"
#include "core/Result.hpp"
#include "core/Sampler.hpp"
#include "core/Workload.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
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

// One finished pass, counted from 1 across the warmup and timed passes.
struct PassDone
{
    std::uint64_t   pass    = 0;
    std::uint64_t   warmups = 0;
    std::uint64_t   passes  = 0;
    Clock::duration wall {};
};

// What the Runner reports as it runs, never while a pass is timed or sampled, so a listener cannot
// slow a measurement.
struct RunEvents
{
    // With the identities and no workloads, once the environment is entered.
    std::function<void(const Result&)> started;
    // Before a workload is created, counting from 1 among the entries the policy admits.
    std::function<void(std::size_t index, std::size_t count, const std::string& name)> workload_started;
    // After each pass, warmups included, where a listener that throws fails the workload.
    std::function<void(const PassDone&)> pass_done;
    // With the workload's result, whatever its outcome.
    std::function<void(const WorkloadResult&)> workload_done;
};

// Runs each entry the policy admits through setup() once, then prepare() and execute() for every
// warmup and timed pass, where a workload that throws, or whose pass differs from its first, fails
// with a reason and the run goes on. Throws WorkloadError before entering the environment for an
// entry name that validate() refuses or that repeats, since results are keyed by name. The probe reads
// the process, through Host unless a test scripts it.
Result run_suite(const std::vector<CatalogEntry>& entries, const Policy& policy, const WorkloadKinds& kinds,
                 RunEnvironment& environment, const ProcessProbe& probe = host_reading, const RunEvents& events = {});

}} // namespace Slic3r::Bench
