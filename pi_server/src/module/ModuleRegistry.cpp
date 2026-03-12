#include "ModuleRegistry.hpp"
#include <iostream>

std::vector<ModuleRegistry::Factory>& ModuleRegistry::factories() {
    static std::vector<Factory> f;
    return f;
}

bool ModuleRegistry::add(Factory factory) {
    factories().push_back(std::move(factory));
    return true;
}

std::vector<std::unique_ptr<RoverModule>> ModuleRegistry::createAll() {
    std::vector<std::unique_ptr<RoverModule>> modules;
    for (auto& f : factories()) {
        auto m = f();
        if (m) {
            std::cout << "[modules] registered: " << m->name() << "\n";
            modules.push_back(std::move(m));
        }
    }
    std::cout << "[modules] " << modules.size() << " module(s) loaded\n";
    return modules;
}
