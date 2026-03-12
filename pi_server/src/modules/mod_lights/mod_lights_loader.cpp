// Auto-registration for mod_lights
#include "module/ModuleRegistry.hpp"
#include "modules/mod_lights/ModLights.hpp"

static bool _reg = ModuleRegistry::add([]() {
    return std::make_unique<ModLights>();
});
