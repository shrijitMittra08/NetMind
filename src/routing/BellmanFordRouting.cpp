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

bool relaxEdge(const DirectedEdge& edge, NodeId src, SearchResult& result) {
    const double fromDistance = result.distance.at(edge.from);
    if (!std::isfinite(fromDistance)) {
        return false;
    }
    const double candidateDistance = fromDistance + edge.weight;
    if (!std::isfinite(candidateDistance)) {
        throw ConfigError("Bellman-Ford produced a non-finite path distance");
    }

    const double currentDistance = result.distance.at(edge.to);
    const NodeId candidateFirstHop = edge.from == src ? edge.to : result.firstHop.at(edge.from);

    bool better = candidateDistance < currentDistance;
    if (candidateDistance == currentDistance && edge.to != src &&
        result.firstHop.count(edge.to) != 0) {
        better = candidateFirstHop < result.firstHop.at(edge.to);
    }
    if (!better) {
        return false;
    }

    result.distance[edge.to] = candidateDistance;
    result.parent[edge.to] = edge.from;
    result.firstHop[edge.to] = candidateFirstHop;
    return true;
}

SearchResult runBellmanFord(const Graph& graph, NodeId src, const WeightPolicy& policy) {
    SearchResult result;
    const Node* source = graph.getNode(src);
    if (source == nullptr || !source->isUp()) {
        return result;
    }

    for (const NodeId id : graph.nodeIds()) {
        const Node* node = graph.getNode(id);
        if (node != nullptr && node->isUp()) {
            result.distance.emplace(id, std::numeric_limits<double>::infinity());
        }
    }
    result.distance[src] = 0.0;

    const std::vector<DirectedEdge> edges = makeDirectedEdges(graph, policy);
    for (std::size_t pass = 1; pass < result.distance.size(); ++pass) {
        bool changed = false;
        for (const DirectedEdge& edge : edges) {
            if (relaxEdge(edge, src, result)) {
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    for (const DirectedEdge& edge : edges) {
        const double fromDistance = result.distance.at(edge.from);
        if (std::isfinite(fromDistance) &&
            fromDistance + edge.weight < result.distance.at(edge.to)) {
            throw ConfigError("Bellman-Ford detected a reachable negative cycle");
        }
    }
    return result;
}

}

std::vector<NodeId> BellmanFordRouting::computeRoute(const Graph& graph, NodeId src, NodeId dst,
                                                     const WeightPolicy& policy) const {
    const SearchResult result = runBellmanFord(graph, src, policy);
    if (result.distance.count(dst) == 0 || !std::isfinite(result.distance.at(dst))) {
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
        if (path.size() > result.parent.size() + 1) {
            return {};
        }
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::unordered_map<NodeId, NodeId> BellmanFordRouting::computeTable(
    const Graph& graph, NodeId src, const WeightPolicy& policy) const {
    return runBellmanFord(graph, src, policy).firstHop;
}

std::string BellmanFordRouting::name() const {
    return "Bellman-Ford";
}

}
