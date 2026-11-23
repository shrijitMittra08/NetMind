#include "netmind/routing/RoutingManager.h"

#include <cmath>
#include <unordered_map>

#include "netmind/common/Errors.h"
#include "netmind/model/ForwardingTable.h"
#include "netmind/model/Router.h"
#include "netmind/routing/RoutingFactory.h"

namespace netmind {
namespace {

using DynamicEntries = std::unordered_map<NodeId, ForwardingTable::Entry>;

DynamicEntries getDynamicEntries(const ForwardingTable& table) {
    DynamicEntries result;
    for (const auto& [destination, entry] : table.toSortedVector()) {
        if (entry.source == ForwardingTable::Source::DYNAMIC) {
            result.emplace(destination, entry);
        }
    }
    return result;
}

std::size_t countChangedEntries(const DynamicEntries& before, const DynamicEntries& after) {
    std::size_t changed = 0;
    for (const auto& [destination, oldEntry] : before) {
        if (after.count(destination) == 0 || after.at(destination).nextHop != oldEntry.nextHop ||
            after.at(destination).cost != oldEntry.cost) {
            ++changed;
        }
    }
    for (const auto& current : after) {
        if (before.count(current.first) == 0) {
            ++changed;
        }
    }
    return changed;
}

double costForPath(const Graph& graph, const std::vector<NodeId>& path,
                   const WeightPolicy& policy) {
    double totalCost = 0.0;
    for (std::size_t index = 1; index < path.size(); ++index) {
        const Link* link = graph.linkBetween(path[index - 1], path[index]);
        const Node* fromNode = graph.getNode(path[index - 1]);
        const Node* toNode = graph.getNode(path[index]);
        if (link == nullptr || !link->isUsable() || fromNode == nullptr || toNode == nullptr ||
            !fromNode->isUp() || !toNode->isUp()) {
            throw TopologyError(
                "Routing algorithm returned a path containing an inactive link or node");
        }

        const double weight = policy.weight(*link);
        if (!std::isfinite(weight) || weight < 0.0) {
            throw ConfigError("Routing manager requires finite, non-negative route costs");
        }
        totalCost += weight;
        if (!std::isfinite(totalCost)) {
            throw ConfigError("Route cost is not finite");
        }
    }
    return totalCost;
}

RoutingChangeSummary recomputeRouter(Graph& graph, Router& router,
                                     const RoutingAlgorithm& algorithm,
                                     const WeightPolicy& policy) {
    const DynamicEntries before = getDynamicEntries(router.forwardingTable());
    const auto nextHops = algorithm.computeTable(graph, router.getId(), policy);
    router.forwardingTable().clearDynamic();

    for (const auto& [destination, nextHop] : nextHops) {
        const std::vector<NodeId> path =
            algorithm.computeRoute(graph, router.getId(), destination, policy);
        if (!path.empty()) {
            router.forwardingTable().install(destination, nextHop,
                                             costForPath(graph, path, policy));
        }
    }

    const DynamicEntries after = getDynamicEntries(router.forwardingTable());
    return RoutingChangeSummary{1, countChangedEntries(before, after)};
}

}

RoutingManager::RoutingManager()
    : algorithm_(RoutingFactory::makeAlgorithm("dijkstra")),
      policy_(RoutingFactory::makePolicy("latency")) {}

void RoutingManager::setAlgorithm(const std::string& name) {
    algorithm_ = RoutingFactory::makeAlgorithm(name);
    algorithmName_ = name;
}

void RoutingManager::setPolicy(const std::string& name) {
    policy_ = RoutingFactory::makePolicy(name);
    policyName_ = name;
}

RoutingChangeSummary RoutingManager::recomputeAll(Graph& graph) {
    RoutingChangeSummary total;
    for (const NodeId id : graph.nodeIds()) {
        auto* router = dynamic_cast<Router*>(graph.getNode(id));
        if (router == nullptr || !router->isUp()) {
            continue;
        }
        const RoutingChangeSummary current = recomputeRouter(graph, *router, *algorithm_, *policy_);
        total.routersProcessed += current.routersProcessed;
        total.entriesChanged += current.entriesChanged;
    }
    return total;
}

RoutingChangeSummary RoutingManager::recomputeFor(Graph& graph, NodeId routerId) {
    auto* router = dynamic_cast<Router*>(graph.getNode(routerId));
    if (router == nullptr) {
        throw TopologyError("Cannot recompute routes for a missing or non-router node");
    }
    if (!router->isUp()) {
        return {};
    }
    return recomputeRouter(graph, *router, *algorithm_, *policy_);
}

std::vector<NodeId> RoutingManager::explainRoute(const Graph& graph, NodeId src, NodeId dst) const {
    return explainRouteDetailed(graph, src, dst).path;
}

RouteExplanation RoutingManager::explainRouteDetailed(const Graph& graph, NodeId src,
                                                      NodeId dst) const {
    RouteExplanation explanation;
    explanation.path = algorithm_->computeRoute(graph, src, dst, *policy_);

    for (std::size_t index = 1; index < explanation.path.size(); ++index) {
        const Link* link = graph.linkBetween(explanation.path[index - 1], explanation.path[index]);
        if (link == nullptr || !link->isUsable()) {
            throw TopologyError("Route explanation contains a missing or inactive link");
        }

        const double weight = policy_->weight(*link);
        if (!std::isfinite(weight) || weight < 0.0) {
            throw ConfigError("Route explanation requires finite, non-negative costs");
        }
        explanation.hopCosts.push_back(weight);
        explanation.totalCost += weight;
        if (!std::isfinite(explanation.totalCost)) {
            throw ConfigError("Route explanation cost is not finite");
        }
    }
    return explanation;
}

void RoutingManager::addStaticRoute(Graph& graph, NodeId routerId, NodeId destination,
                                    NodeId nextHop) {
    auto* router = dynamic_cast<Router*>(graph.getNode(routerId));
    if (router == nullptr) {
        throw TopologyError("Static route source must be an existing router");
    }
    if (!router->isUp()) {
        throw TopologyError("Cannot add a static route to a down router");
    }
    if (graph.getNode(destination) == nullptr) {
        throw TopologyError("Static route destination does not exist");
    }
    const Node* nextHopNode = graph.getNode(nextHop);
    if (nextHopNode == nullptr) {
        throw TopologyError("Static route next hop does not exist");
    }

    const Link* outgoingLink = graph.linkBetween(routerId, nextHop);
    if (outgoingLink == nullptr || !outgoingLink->isUsable() || !nextHopNode->isUp()) {
        throw TopologyError("Static route next hop must be an active neighbor");
    }

    const double cost = policy_->weight(*outgoingLink);
    if (!std::isfinite(cost) || cost < 0.0) {
        throw ConfigError("Static route cost must be finite and non-negative");
    }
    router->forwardingTable().addStatic(destination, nextHop, cost);
}

void RoutingManager::setReconvergenceDelay(double delay) {
    if (!std::isfinite(delay) || delay < 0.0) {
        throw ConfigError("Reconvergence delay must be finite and non-negative");
    }
    reconvergenceDelay_ = delay;
}

double RoutingManager::reconvergenceDelay() const noexcept {
    return reconvergenceDelay_;
}

std::string RoutingManager::algorithmName() const {
    return algorithmName_;
}

std::string RoutingManager::policyName() const {
    return policyName_;
}

}
