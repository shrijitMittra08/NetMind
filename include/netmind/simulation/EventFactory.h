#pragma once

#include "netmind/simulation/NetworkEvent.h"

#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace netmind {

class Graph;

class EventFactory {
public:
    [[nodiscard]] static std::shared_ptr<NetworkEvent> create(
        const nlohmann::json& event,
        const Graph& graph);
};

}