#pragma once
// ============================================================
//  ModuleRegistry — static auto-registration for modules
//
//  Each module's loader.cpp adds a factory via:
//      static bool _reg = ModuleRegistry::add([]() {
//          return std::make_unique<MyModule>();
//      });
// ============================================================
#include "RoverModule.hpp"
#include <vector>
#include <functional>
#include <memory>

class ModuleRegistry {
public:
    using Factory = std::function<std::unique_ptr<RoverModule>()>;

    // Register a factory — called from static initialisers
    static bool add(Factory factory);

    // Instantiate every registered module
    static std::vector<std::unique_ptr<RoverModule>> createAll();

private:
    static std::vector<Factory>& factories();
};
