#pragma once

#include "core/Result.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <optional>
#include <string>

namespace Slic3r { namespace Bench {

// A fixture's model, centered on the bed hermetic_config() describes, or why it is not available.
struct FixtureModel
{
    std::optional<Model> model;
    // Empty when the model is there.
    std::string unavailable;
};

// Reads "handy:<file>", one of the models under resources_dir()'s handy_models, or builds
// "procedural:<shape>", and throws std::invalid_argument for an id that names neither.
FixtureModel load_fixture(const std::string& id);

// full_print_config() on a bed every handy model fits on, with the layer change G-code validate()
// requires and object labels off, then the keys given, throwing for a key the slicer does not know or
// a value it cannot read.
DynamicPrintConfig hermetic_config(const Properties& keys);

}} // namespace Slic3r::Bench
