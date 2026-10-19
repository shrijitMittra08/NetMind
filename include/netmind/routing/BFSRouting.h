#pragma once

#include "netmind/routing/RoutingAlgorithm.h"

namespace netmind {

class BFSRouting : public RoutingAlgorithm {
public:
    [[nodiscard]] std::vector<NodeId> computeRoute(
        const Graph& graph, NodeId src, NodeId dst,
        const WeightPolicy& policy) const override;

    [[nodiscard]] std::unordered_map<NodeId, NodeId>
    computeTable(
        const Graph& graph, NodeId src,
        const WeightPolicy& policy) const override;

    [[nodiscard]] std::string name() const override;
};

}