#include "netmind/routing/RoutingManager.h"

#include <cmath>
#include <unordered_map>
#include <utility>

#include "netmind/common/Errors.h"
#include "netmind/model/ForwardingTable.h"
#include "netmind/model/Router.h"
#include "netmind/routing/RoutingFactory.h"

namespace netmind {
namespace {

using DynamicEntries = std::unordered_map<NodeId, ForwardingTable::Entry>;

DynamicEntries getDynamicEntries(const ForwardingTable& table) {
    DynamicEntries result;

    for (const auto& route : table.toSortedVector()) {
        if (route.second.source == ForwardingTable::Source::DYNAMIC) {
            result.emplace(route.first, route.second);
        }
    }

    return result;
}

std::size_t countChangedEntries(const DynamicEntries& before, const DynamicEntries& after) {
    std::size_t changed = 0;

    for (const auto& previous : before) {
        const auto current = after.find(previous.first);

        if (current == after.end() || current->second.nextHop != previous.second.nextHop ||
            current->second.cost != previous.second.cost) {
            ++changed;
        }
    }

    for (const auto& current : after) {
        if (before.find(current.first) == before.end()) {
            ++changed;
        }
    }

    return changed;
}

double costForPath(const Graph& graph, const std::vector<NodeId>& path,
                   const WeightPolicy& policy) {
    double totalCost = 0.0;

    for (std::size_t index = 1; index < path.size(); ++index) {
        const NodeId from = path[index - 1];
        const NodeId to = path[index];
        const Link* link = graph.linkBetween(from, to);

        const Node* fromNode = graph.getNode(from);
        const Node* toNode = graph.getNode(to);

        if (link == nullptr || !link->isUsable() || fromNode == nullptr || toNode == nullptr ||
            !fromNode->isUp() || !toNode->isUp()) {
            throw TopologyError(
                "Routing algorithm returned a path containing an "
                "inactive link or node");
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

    for (const auto& route : nextHops) {
        const auto path = algorithm.computeRoute(graph, router.getId(), route.first, policy);

        if (path.empty()) {
            continue;
        }

        const double cost = costForPath(graph, path, policy);
        router.forwardingTable().install(route.first, route.second, cost);
    }

    const DynamicEntries after = getDynamicEntries(router.forwardingTable());

    return RoutingChangeSummary{1, countChangedEntries(before, after)};
}

}

RoutingManager::RoutingManager()
    : algorithm_(RoutingFactory::makeAlgorithm("dijkstra")),
      policy_(RoutingFactory::makePolicy("latency")) {}

void RoutingManager::setAlgorithm(const std::string& name) {
    auto newAlgorithm = RoutingFactory::makeAlgorithm(name);
    algorithm_ = std::move(newAlgorithm);
    algorithmName_ = name;
}

void RoutingManager::setPolicy(const std::string& name) {
    auto newPolicy = RoutingFactory::makePolicy(name);
    policy_ = std::move(newPolicy);
    policyName_ = name;
}

RoutingChangeSummary RoutingManager::recomputeAll(Graph& graph) {
    RoutingChangeSummary total;

    for (const NodeId id : graph.nodeIds()) {
        Node* node = graph.getNode(id);
        auto* router = dynamic_cast<Router*>(node);

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
    Node* node = graph.getNode(routerId);
    auto* router = dynamic_cast<Router*>(node);

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

    if (explanation.path.empty()) {
        return explanation;
    }

    explanation.hopCosts.reserve(explanation.path.size() - 1);

    for (std::size_t index = 1; index < explanation.path.size(); ++index) {
        const NodeId from = explanation.path[index - 1];
        const NodeId to = explanation.path[index];
        const Link* link = graph.linkBetween(from, to);

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
    Node* node = graph.getNode(routerId);
    auto* router = dynamic_cast<Router*>(node);

    if (router == nullptr) {
        throw TopologyError("Static route source must be an existing router");
    }

    if (!router->isUp()) {
        throw TopologyError("Cannot add a static route to a down router");
    }

    if (graph.getNode(destination) == nullptr) {
        throw TopologyError("Static route destination does not exist");
    }

    if (graph.getNode(nextHop) == nullptr) {
        throw TopologyError("Static route next hop does not exist");
    }

    const Link* outgoingLink = graph.linkBetween(routerId, nextHop);
    const Node* nextHopNode = graph.getNode(nextHop);

    if (outgoingLink == nullptr || !outgoingLink->isUsable() || nextHopNode == nullptr ||
        !nextHopNode->isUp()) {
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