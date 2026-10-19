#include "netmind/routing/BFSRouting.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "netmind/routing/WeightPolicy.h"

namespace netmind {

std::vector<NodeId> BFSRouting::computeRoute(const Graph& graph, NodeId src, NodeId dst,
                                             const WeightPolicy&) const {
    std::vector<NodeId> path;
    if (!graph.bfsPath(src, dst, path)) {
        return {};
    }

    return path;
}

std::unordered_map<NodeId, NodeId> BFSRouting::computeTable(const Graph& graph, NodeId src,
                                                            const WeightPolicy&) const {
    std::unordered_map<NodeId, NodeId> nextHopByDestination;

    const Node* source = graph.getNode(src);
    if (source == nullptr || !source->isUp()) {
        return nextHopByDestination;
    }

    std::queue<NodeId> pending;
    std::unordered_set<NodeId> visited;
    std::unordered_map<NodeId, NodeId> firstHop;

    pending.push(src);
    visited.insert(src);

    while (!pending.empty()) {
        const NodeId current = pending.front();
        pending.pop();

        std::vector<NodeId> neighbors;
        for (const Link* link : graph.getActiveNeighbours(current)) {
            neighbors.push_back(link->other(current));
        }
        std::sort(neighbors.begin(), neighbors.end());

        for (const NodeId neighbor : neighbors) {
            if (!visited.insert(neighbor).second) {
                continue;
            }

            firstHop[neighbor] = current == src ? neighbor : firstHop.at(current);

            pending.push(neighbor);
        }
    }

    nextHopByDestination.reserve(firstHop.size());
    for (const auto& entry : firstHop) {
        nextHopByDestination.emplace(entry.first, entry.second);
    }

    return nextHopByDestination;
}

std::string BFSRouting::name() const {
    return "BFS";
}

}