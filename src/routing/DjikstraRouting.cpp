#include "netmind/routing/DjikstraRouting.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>
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

    MinHeap<std::pair<double, NodeId>> pending;
    result.distance.emplace(src, 0.0);
    pending.push({0.0, src});

    while (!pending.empty()) {
        const auto [currentDistance, current] = pending.extractMin();
        if (result.distance.count(current) == 0 ||
            currentDistance != result.distance.at(current)) {
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
            const NodeId candidateFirstHop = current == src ? neighbor : result.firstHop.at(current);

            bool better = result.distance.count(neighbor) == 0 ||
                          candidateDistance < result.distance.at(neighbor);
            if (!better && candidateDistance == result.distance.at(neighbor) &&
                result.firstHop.count(neighbor) != 0) {
                better = candidateFirstHop < result.firstHop.at(neighbor);
            }
            if (!better) {
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

}

std::vector<NodeId> DijkstraRouting::computeRoute(const Graph& graph, NodeId src, NodeId dst,
                                                  const WeightPolicy& policy) const {
    const SearchResult result = runDijkstra(graph, src, policy);
    if (result.distance.count(dst) == 0) {
        return {};
    }

    std::vector<NodeId> path{dst};
    NodeId current = dst;
    while (current != src) {
        if (result.parent.count(current) == 0) {
            return {};
        }
        current = result.parent.at(current);
        path.push_back(current);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::unordered_map<NodeId, NodeId> DijkstraRouting::computeTable(const Graph& graph, NodeId src,
                                                                 const WeightPolicy& policy) const {
    return runDijkstra(graph, src, policy).firstHop;
}

std::string DijkstraRouting::name() const {
    return "Dijkstra";
}

}
