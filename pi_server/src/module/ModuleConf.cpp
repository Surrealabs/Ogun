#include "ModuleConf.hpp"
#include <fstream>

std::map<std::string, std::string> loadModuleConf(const std::string& path) {
    std::map<std::string, std::string> conf;
    std::ifstream f(path);
    if (!f.is_open()) return conf;

    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };

    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        conf[trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
    }
    return conf;
}
