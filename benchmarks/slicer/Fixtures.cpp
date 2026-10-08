#include "slicer/Fixtures.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/operations.hpp>

#include <stdexcept>
#include <string_view>
#include <utility>

namespace Slic3r { namespace Bench {

namespace {

constexpr double bed_size = 350.;

constexpr std::string_view handy_prefix      = "handy:";
constexpr std::string_view procedural_prefix = "procedural:";

bool starts_with(const std::string& text, std::string_view prefix) { return text.compare(0, prefix.size(), prefix) == 0; }

Model procedural(const std::string& shape)
{
    Model model;
    if (shape == "smoke-cube")
        model.add_object(shape.c_str(), "", make_cube(20., 20., 20.))->add_instance();
    else
        throw std::invalid_argument("no procedural shape is called '" + shape + "'");
    return model;
}

} // namespace

FixtureModel load_fixture(const std::string& id)
{
    FixtureModel fixture;
    Model        model;
    if (starts_with(id, handy_prefix)) {
        const boost::filesystem::path path = boost::filesystem::path(resources_dir()) / "handy_models" / id.substr(handy_prefix.size());
        if (!boost::filesystem::exists(path)) {
            fixture.unavailable = path.string() + " is missing";
            return fixture;
        }
        model = Model::read_from_file(path.string(), nullptr, nullptr, LoadStrategy::AddDefaultInstances);
    } else if (starts_with(id, procedural_prefix)) {
        model = procedural(id.substr(procedural_prefix.size()));
    } else {
        throw std::invalid_argument("the fixture '" + id + "' is neither handy: nor procedural:");
    }
    model.center_instances_around_point(Vec2d(bed_size / 2, bed_size / 2));
    for (ModelObject* object : model.objects)
        object->ensure_on_bed();
    fixture.model = std::move(model);
    return fixture;
}

DynamicPrintConfig hermetic_config(const Properties& keys)
{
    // apply() creates each option from its definition, so the enum lists keep the keys map they read and write
    // names with, which the copies full_print_config() makes lack.
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    const std::string size = std::to_string(int(bed_size));
    config.set_deserialize_strict({{"printable_area", "0x0," + size + "x0," + size + "x" + size + ",0x" + size},
                                   {"printable_height", size},
                                   {"layer_change_gcode", "G92 E0"},
                                   // Object label comments print an id only the cancel-object feature sets, which
                                   // otherwise differs between passes.
                                   {"gcode_label_objects", "0"}});
    for (const auto& [key, value] : keys) {
        // A key the slicer does not know only lands in the context, which the strict setter ignores.
        ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Disable);
        config.set_deserialize(key, value, context);
        if (!context.unrecogized_keys.empty())
            throw std::invalid_argument("the slicer has no setting called '" + key + "'");
    }
    return config;
}

}} // namespace Slic3r::Bench
