#pragma once

#include "netmind/graph/Graph.h"

#include <nlohmann/json.hpp>

#include <string>

namespace netmind {

class TopologyLoader {
public:
    [[nodiscard]] static Graph loadFromFile(const std::string& filePath);
    [[nodiscard]] static Graph loadFromJson(const nlohmann::json& document);
};

}