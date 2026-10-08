#include "core/Workload.hpp"
#include "slicer/Fixtures.hpp"

#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/operations.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace Bench {

namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime  = 1099511628211ull;

std::uint64_t fnv1a(std::uint64_t hash, const void* data, std::size_t size)
{
    for (const unsigned char* byte = static_cast<const unsigned char*>(data); size > 0; --size, ++byte) {
        hash ^= *byte;
        hash *= fnv_prime;
    }
    return hash;
}

template<typename T> std::uint64_t fnv1a(std::uint64_t hash, const std::vector<T>& items)
{ return fnv1a(hash, items.data(), items.size() * sizeof(T)); }

std::uint64_t fnv1a(std::uint64_t hash, const Transform3d& placement)
{ return fnv1a(hash, placement.data(), placement.matrix().size() * sizeof(double)); }

// FNV-1a over each part's vertices, triangles and placement and each instance's placement, so the names and
// settings a file carries do not count.
std::uint64_t geometry_hash(const Model& model)
{
    std::uint64_t hash = fnv_offset;
    for (const ModelObject* object : model.objects) {
        for (const ModelVolume* volume : object->volumes) {
            hash = fnv1a(hash, volume->mesh().its.vertices);
            hash = fnv1a(hash, volume->mesh().its.indices);
            hash = fnv1a(hash, volume->get_matrix());
        }
        for (const ModelInstance* instance : object->instances)
            hash = fnv1a(hash, instance->get_matrix());
    }
    return hash;
}

// Reads a fixture's file, or an STL written from the fixture when the entry's format asks, timing the
// read as Load's one step.
class LoadWorkload : public Workload
{
public:
    explicit LoadWorkload(CatalogEntry entry) : m_entry(std::move(entry)) {}
    ~LoadWorkload() override { free_model(); }

    std::optional<std::string> setup(const RunContext&) override
    {
        std::string format;
        for (const auto& [key, value] : m_entry.config) {
            if (key != "format")
                throw std::invalid_argument("a load has no setting called '" + key + "'");
            if (value != "stl" && value != "stl-ascii")
                throw std::invalid_argument("a load's format is stl or stl-ascii, not '" + value + "'");
            format = value;
        }
        if (format.empty()) {
            const std::optional<std::string> path = fixture_path(m_entry.fixture);
            if (!path)
                throw std::invalid_argument(m_entry.fixture + " has no file to read, so the entry needs a format");
            if (!boost::filesystem::exists(*path))
                return "fixture not available: " + *path + " is missing";
            m_path = *path;
            return std::nullopt;
        }
        FixtureModel fixture = load_fixture(m_entry.fixture);
        if (!fixture.model)
            return "fixture not available: " + fixture.unavailable;
        m_path = (boost::filesystem::path(temporary_dir()) / boost::filesystem::unique_path("%%%%-%%%%-%%%%.stl")).string();
        if (!store_stl(m_path.c_str(), &*fixture.model, format == "stl"))
            throw std::runtime_error("cannot write " + m_path);
        return std::nullopt;
    }

    // The last pass's model goes here, so freeing it is never timed.
    void prepare(const RunContext&) override { free_model(); }

    void execute(const RunContext& context, Measurement& measurement) override
    {
        if (context.timed.count(Stage::Load) == 0)
            return;
        // A project's settings are read as opening it in the app reads them.
        DynamicPrintConfig        config;
        ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Enable);
        const Clock::time_point   started_at = Clock::now();
        m_model = Model::read_from_file(m_path, &config, &substitutions,
                                        LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances);
        const Clock::time_point done_at = Clock::now();

        double facets = 0., volumes = 0.;
        for (const ModelObject* object : m_model->objects)
            for (const ModelVolume* volume : object->volumes) {
                facets += double(volume->mesh().facets_count());
                ++volumes;
            }
        measurement.span("read_from_file", Scope::print(), started_at, done_at,
                         {{"facets", facets}, {"objects", double(m_model->objects.size())}, {"volumes", volumes}});
        measurement.output_hash(geometry_hash(*m_model));
    }

private:
    // Deletes the backup folder a 3MF read makes, so the model's destructor does not start libslic3r's backup
    // thread, which can deadlock the process's exit.
    void free_model()
    {
        if (m_model)
            m_model->remove_backup_path_if_exist();
        m_model.reset();
    }

    CatalogEntry         m_entry;
    std::string          m_path;
    std::optional<Model> m_model;
};

const WorkloadKindRegistrar registrar("load", [](const CatalogEntry& entry) -> std::unique_ptr<Workload> {
    return std::make_unique<LoadWorkload>(entry);
});

} // namespace

}} // namespace Slic3r::Bench
