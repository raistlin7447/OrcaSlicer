#pragma once

#include <functional>
#include <type_traits>
#include <vector>

namespace Slic3r {

class ModelObject;
struct PlateData;
typedef std::vector<PlateData*> PlateDataPtrs;

typedef std::function<void(int import_stage, int current, int total, bool& cancel)> Import3mfProgressFn;

enum class LoadStrategy
{
    Default = 0,
    AddDefaultInstances = 1,
    CheckVersion = 2,
    LoadModel = 4,
    LoadConfig = 8,
    LoadAuxiliary = 16,
    Silence = 32,
    ImperialUnits = 64,

    Restore = 0x10000 | LoadModel | LoadConfig | LoadAuxiliary | Silence,
};

inline LoadStrategy operator | (LoadStrategy lhs, LoadStrategy rhs)
{
    using T = std::underlying_type_t <LoadStrategy>;
    return static_cast<LoadStrategy>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

inline bool operator & (LoadStrategy & lhs, LoadStrategy rhs)
{
    using T = std::underlying_type_t <LoadStrategy>;
    return (static_cast<T>(lhs) & static_cast<T>(rhs)) == static_cast<T>(rhs);
}

extern void save_object_mesh(ModelObject& object);

class SaveObjectGaurd {
public:
    SaveObjectGaurd(ModelObject& object);
    ~SaveObjectGaurd();
};

} // namespace Slic3r
