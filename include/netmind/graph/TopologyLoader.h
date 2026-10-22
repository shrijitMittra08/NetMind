#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "netmind/graph/Graph.h"

namespace netmind {

class TopologyLoader {
  public:
    [[nodiscard]] static Graph loadFromFile(const std::string& filePath);
    [[nodiscard]] static Graph loadFromJson(const nlohmann::json& document);
};

}