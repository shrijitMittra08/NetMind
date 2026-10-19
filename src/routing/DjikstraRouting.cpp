#include "netmind/routing/DjikstraRouting.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

#include "netmind/common/Errors.h"
#include "netmind/common/MinHeap.h"
#include "netmind/routing/WeightPolicy.h"

namespace netmind {
namespace {

struct SearchResult {
    std::unordered_map<NodeId, double> distance;
    std::unordered_map<NodeId, NodeId> parent;
    std::unordered_map<NodeId, NodeId> firstHop;
};

SearchResult runDijkstra(const Graph& graph, NodeId src, const WeightPolicy& policy) {
    SearchResult result;

    const Node* source = graph.getNode(src);
    if (source == nullptr || !source->isUp()) {
        return result;
    }

    using QueueEntry = std::pair<double, NodeId>;
    MinHeap<QueueEntry> pending;

    result.distance.emplace(src, 0.0);
    pending.push({0.0, src});

    while (!pending.empty()) {
        const QueueEntry entry = pending.extractMin();
        const double currentDistance = entry.first;
        const NodeId current = entry.second;

        const auto knownDistance = result.distance.find(current);
        if (knownDistance == result.distance.end() || currentDistance != knownDistance->second) {
            continue;
        }

        std::vector<const Link*> links = graph.getActiveNeighbours(current);

        std::sort(links.begin(), links.end(), [current](const Link* left, const Link* right) {
            return left->other(current) < right->other(current);
        });

        for (const Link* link : links) {
            const NodeId neighbor = link->other(current);

            if (neighbor == src) {
                continue;
            }

            const double edgeWeight = policy.weight(*link);
            if (!std::isfinite(edgeWeight) || edgeWeight < 0.0) {
                throw ConfigError("Dijkstra requires finite, non-negative link weights");
            }

            const double candidateDistance = currentDistance + edgeWeight;
            if (!std::isfinite(candidateDistance)) {
                throw ConfigError("Dijkstra produced a non-finite path distance");
            }

            const NodeId candidateFirstHop =
                current == src ? neighbor : result.firstHop.at(current);

            const auto neighborDistance = result.distance.find(neighbor);

            bool shouldUpdate = neighborDistance == result.distance.end() ||
                                candidateDistance < neighborDistance->second;

            if (!shouldUpdate && neighborDistance != result.distance.end() &&
                candidateDistance == neighborDistance->second) {
                const auto existingFirstHop = result.firstHop.find(neighbor);

                shouldUpdate = existingFirstHop != result.firstHop.end() &&
                               candidateFirstHop < existingFirstHop->second;
            }

            if (!shouldUpdate) {
                continue;
            }

            result.distance[neighbor] = candidateDistance;
            result.parent[neighbor] = current;
            result.firstHop[neighbor] = candidateFirstHop;
            pending.push({candidateDistance, neighbor});
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
    }

    std::reverse(path.begin(), path.end());
    return path;
}

}

std::vector<NodeId> DijkstraRouting::computeRoute(const Graph& graph, NodeId src, NodeId dst,
                                                  const WeightPolicy& policy) const {
    const SearchResult result = runDijkstra(graph, src, policy);

    if (result.distance.find(dst) == result.distance.end()) {
        return {};
    }

    return reconstructPath(src, dst, result.parent);
}

std::unordered_map<NodeId, NodeId> DijkstraRouting::computeTable(const Graph& graph, NodeId src,
                                                                 const WeightPolicy& policy) const {
    const SearchResult result = runDijkstra(graph, src, policy);
    return result.firstHop;
}

std::string DijkstraRouting::name() const {
    return "Dijkstra";
}

}