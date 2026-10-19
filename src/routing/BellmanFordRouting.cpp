#include "netmind/routing/BellmanFordRouting.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#include "netmind/common/Errors.h"
#include "netmind/routing/WeightPolicy.h"

namespace netmind {
namespace {

struct DirectedEdge {
    NodeId from{};
    NodeId to{};
    double weight{};
};

struct SearchResult {
    std::unordered_map<NodeId, double> distance;
    std::unordered_map<NodeId, NodeId> parent;
    std::unordered_map<NodeId, NodeId> firstHop;
};

std::vector<DirectedEdge> makeDirectedEdges(const Graph& graph, const WeightPolicy& policy) {
    std::vector<DirectedEdge> edges;

    for (const NodeId id : graph.nodeIds()) {
        for (const Link* link : graph.getActiveNeighbours(id)) {
            const NodeId neighbor = link->other(id);

            if (id >= neighbor) {
                continue;
            }

            const double weight = policy.weight(*link);
            if (!std::isfinite(weight)) {
                throw ConfigError("Bellman-Ford requires finite link weights");
            }

            edges.push_back({id, neighbor, weight});
            edges.push_back({neighbor, id, weight});
        }
    }

    std::sort(edges.begin(), edges.end(), [](const DirectedEdge& left, const DirectedEdge& right) {
        if (left.from != right.from) {
            return left.from < right.from;
        }
        return left.to < right.to;
    });

    return edges;
}

SearchResult runBellmanFord(const Graph& graph, NodeId src, const WeightPolicy& policy) {
    SearchResult result;

    const Node* source = graph.getNode(src);
    if (source == nullptr || !source->isUp()) {
        return result;
    }

    const double infinity = std::numeric_limits<double>::infinity();

    for (const NodeId id : graph.nodeIds()) {
        const Node* node = graph.getNode(id);
        if (node != nullptr && node->isUp()) {
            result.distance.emplace(id, infinity);
        }
    }

    result.distance[src] = 0.0;

    const std::vector<DirectedEdge> edges = makeDirectedEdges(graph, policy);
    const std::size_t nodeCount = result.distance.size();

    for (std::size_t pass = 1; pass < nodeCount; ++pass) {
        bool changed = false;

        for (const DirectedEdge& edge : edges) {
            const double fromDistance = result.distance.at(edge.from);
            if (!std::isfinite(fromDistance)) {
                continue;
            }

            const double candidateDistance = fromDistance + edge.weight;
            if (!std::isfinite(candidateDistance)) {
                throw ConfigError("Bellman-Ford produced a non-finite path distance");
            }

            const auto toDistance = result.distance.find(edge.to);
            const bool shorter = candidateDistance < toDistance->second;

            NodeId candidateFirstHop = edge.to;
            if (edge.from != src) {
                candidateFirstHop = result.firstHop.at(edge.from);
            }

            bool betterTie = false;
            if (!shorter && candidateDistance == toDistance->second && edge.to != src) {
                const auto existingFirstHop = result.firstHop.find(edge.to);

                betterTie = existingFirstHop != result.firstHop.end() &&
                            candidateFirstHop < existingFirstHop->second;
            }

            if (!shorter && !betterTie) {
                continue;
            }

            result.distance[edge.to] = candidateDistance;
            result.parent[edge.to] = edge.from;
            result.firstHop[edge.to] = candidateFirstHop;
            changed = true;
        }

        if (!changed) {
            break;
        }
    }

    for (const DirectedEdge& edge : edges) {
        const double fromDistance = result.distance.at(edge.from);
        if (!std::isfinite(fromDistance)) {
            continue;
        }

        const double candidateDistance = fromDistance + edge.weight;

        if (candidateDistance < result.distance.at(edge.to)) {
            throw ConfigError("Bellman-Ford detected a reachable negative cycle");
        }
    }

    return result;
}

std::vector<NodeId> reconstructPath(NodeId src, NodeId dst,
                                    const std::unordered_map<NodeId, NodeId>& parent) {
    std::vector<NodeId> path;
    NodeId current = dst;
    path.push_back(current);

    while (current != src) {
        const auto parentIt = parent.find(current);
        if (parentIt == parent.end()) {
            return {};
        }

        current = parentIt->second;
        path.push_back(current);

        if (path.size() > parent.size() + 1) {
            return {};
        }
    }

    std::reverse(path.begin(), path.end());
    return path;
}

}

std::vector<NodeId> BellmanFordRouting::computeRoute(const Graph& graph, NodeId src, NodeId dst,
                                                     const WeightPolicy& policy) const {
    const SearchResult result = runBellmanFord(graph, src, policy);

    const auto destination = result.distance.find(dst);
    if (destination == result.distance.end() || !std::isfinite(destination->second)) {
        return {};
    }

    return reconstructPath(src, dst, result.parent);
}

std::unordered_map<NodeId, NodeId> BellmanFordRouting::computeTable(
    const Graph& graph, NodeId src, const WeightPolicy& policy) const {
    const SearchResult result = runBellmanFord(graph, src, policy);
    return result.firstHop;
}

std::string BellmanFordRouting::name() const {
    return "Bellman-Ford";
}

}