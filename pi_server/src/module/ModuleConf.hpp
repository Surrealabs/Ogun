#pragma once
// Load an INI-style (key = value, # comments) config file into a map.
// Returns an empty map if the file doesn't exist.
#include <string>
#include <map>

std::map<std::string, std::string> loadModuleConf(const std::string& path);
