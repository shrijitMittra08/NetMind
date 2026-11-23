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
    std::unordered_map<NodeId, NodeId> firstHop;
    const Node* source = graph.getNode(src);
    if (source == nullptr || !source->isUp()) {
        return firstHop;
    }

    std::queue<NodeId> pending;
    std::unordered_set<NodeId> visited{src};
    pending.push(src);

    while (!pending.empty()) {
        const NodeId current = pending.front();
        pending.pop();

        std::vector<NodeId> neighbors;
        for (const Link* link : graph.getActiveNeighbours(current)) {
            neighbors.push_back(link->other(current));
        }
        std::sort(neighbors.begin(), neighbors.end());

        for (const NodeId neighbor : neighbors) {
            if (visited.count(neighbor) != 0) {
                continue;
            }
            visited.insert(neighbor);
            firstHop[neighbor] = current == src ? neighbor : firstHop.at(current);
            pending.push(neighbor);
        }
    }
    return firstHop;
}

std::string BFSRouting::name() const {
    return "BFS";
}

}
