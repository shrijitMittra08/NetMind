#pragma once

#include "netmind/graph/Graph.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace netmind {

class WeightPolicy;

class RoutingAlgorithm {
public:
    virtual ~RoutingAlgorithm() = default;

    [[nodiscard]] virtual std::vector<NodeId> computeRoute(
        const Graph& graph,
        NodeId src,
        NodeId dst,
        const WeightPolicy& policy) const = 0;

    [[nodiscard]] virtual std::unordered_map<NodeId, NodeId>
    computeTable(
        const Graph& graph,
        NodeId src,
        const WeightPolicy& policy) const = 0;

    [[nodiscard]] virtual std::string name() const = 0;
};

}