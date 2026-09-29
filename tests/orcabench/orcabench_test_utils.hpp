#pragma once

#include "core/Workload.hpp"

#include <optional>
#include <string>
#include <utility>

namespace Slic3r { namespace Bench { namespace Test {

// A workload that slices nothing, so the framework around it tests in milliseconds.
class FakeWorkload : public Workload
{
public:
    explicit FakeWorkload(CatalogEntry entry) : entry(std::move(entry)) {}

    std::optional<std::string> setup(const RunContext&) override { return std::nullopt; }
    void                       prepare(const RunContext&) override {}
    void                       execute(const RunContext&, Measurement&) override {}

    // The entry the registry built it from.
    const CatalogEntry entry;
};

}}} // namespace Slic3r::Bench::Test
