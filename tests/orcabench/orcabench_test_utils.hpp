#pragma once

#include "core/Runner.hpp"
#include "core/Workload.hpp"

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace Bench { namespace Test {

// What a FakeWorkload does beyond logging each call, where an unset hook does nothing and a pass
// counts from 1 across the warmup and timed passes.
struct FakeHooks
{
    std::function<std::optional<std::string>(const RunContext&)> setup;
    std::function<void(unsigned pass)>                           prepare;
    std::function<void(unsigned pass, Measurement&)>             execute;
};

// A workload that slices nothing, so the framework around it tests in milliseconds.
class FakeWorkload : public Workload
{
public:
    explicit FakeWorkload(CatalogEntry entry, FakeHooks hooks = {}, std::vector<std::string>* calls = nullptr)
        : entry(std::move(entry)), m_hooks(std::move(hooks)), m_calls(calls)
    {}

    std::optional<std::string> setup(const RunContext& context) override
    {
        log("setup");
        if (!m_hooks.setup)
            return std::nullopt;
        return m_hooks.setup(context);
    }

    void prepare(const RunContext&) override
    {
        log("prepare");
        ++m_pass;
        if (m_hooks.prepare)
            m_hooks.prepare(m_pass);
    }

    void execute(const RunContext&, Measurement& measurement) override
    {
        log("execute");
        if (m_hooks.execute)
            m_hooks.execute(m_pass, measurement);
    }

    // The entry the registry built it from.
    const CatalogEntry entry;

private:
    void log(const char* call)
    {
        if (m_calls)
            m_calls->push_back(call);
    }

    FakeHooks                 m_hooks;
    std::vector<std::string>* m_calls;
    unsigned                  m_pass = 0;
};

// A run environment that logs being entered and left, and throws from enter() when told to refuse.
class FakeEnvironment : public RunEnvironment
{
public:
    explicit FakeEnvironment(std::vector<std::string>& calls, bool refuse = false) : m_calls(calls), m_refuse(refuse) {}

    void enter(const Policy& policy) override
    {
        m_calls.push_back("enter");
        threads = policy.threads;
        if (m_refuse)
            throw std::runtime_error("the environment refused to enter");
    }

    void leave() noexcept override { m_calls.push_back("leave"); }

    // The thread count the run entered with.
    unsigned threads = 0;

private:
    std::vector<std::string>& m_calls;
    bool                      m_refuse;
};

}}} // namespace Slic3r::Bench::Test
