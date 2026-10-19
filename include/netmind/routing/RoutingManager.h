#pragma once

#include "netmind/graph/Graph.h"
#include "netmind/routing/RoutingAlgorithm.h"
#include "netmind/routing/WeightPolicy.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace netmind {

struct RoutingChangeSummary {
    std::size_t routersProcessed{};
    std::size_t entriesChanged{};
};

struct RouteExplanation {
    std::vector<NodeId> path;
    std::vector<double> hopCosts;
    double totalCost{};
};

class RoutingManager {
public:
    RoutingManager();

    void setAlgorithm(const std::string& name);
    void setPolicy(const std::string& name);

    [[nodiscard]] RoutingChangeSummary recomputeAll(Graph& graph);
    [[nodiscard]] RoutingChangeSummary recomputeFor(
        Graph& graph,
        NodeId routerId
    );

    [[nodiscard]] std::vector<NodeId> explainRoute(
        const Graph& graph,
        NodeId src,
        NodeId dst
    ) const;

    [[nodiscard]] RouteExplanation explainRouteDetailed(
        const Graph& graph,
        NodeId src,
        NodeId dst
    ) const;

    void addStaticRoute(
        Graph& graph,
        NodeId routerId,
        NodeId destination,
        NodeId nextHop
    );

    void setReconvergenceDelay(double delay);
    [[nodiscard]] double reconvergenceDelay() const noexcept;

    [[nodiscard]] std::string algorithmName() const;
    [[nodiscard]] std::string policyName() const;

private:
    std::unique_ptr<RoutingAlgorithm> algorithm_;
    std::unique_ptr<WeightPolicy> policy_;
    std::string algorithmName_{"dijkstra"};
    std::string policyName_{"latency"};
    double reconvergenceDelay_{0.0};
};

}