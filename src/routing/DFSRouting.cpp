#include "netmind/routing/DFSRouting.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

#include "netmind/routing/WeightPolicy.h"

namespace netmind {
namespace {

struct DfsForest {
    std::unordered_map<NodeId, NodeId> parent;
    std::unordered_map<NodeId, NodeId> root;
    std::unordered_map<NodeId, std::vector<NodeId>> children;
};

struct DfsFrame {
    NodeId node;
    std::vector<NodeId> neighbors;
    std::size_t nextNeighbor{};
};

std::vector<NodeId> sortedActiveNeighbors(const Graph& graph, NodeId node) {
    std::vector<NodeId> neighbors;
    for (const Link* link : graph.getActiveNeighbours(node)) {
        neighbors.push_back(link->other(node));
    }
    std::sort(neighbors.begin(), neighbors.end());
    return neighbors;
}

DfsForest buildDfsForest(const Graph& graph) {
    DfsForest forest;
    for (const NodeId start : graph.nodeIds()) {
        const Node* startNode = graph.getNode(start);
        if (startNode == nullptr || !startNode->isUp() || forest.parent.count(start) != 0) {
            continue;
        }
        forest.parent.emplace(start, start);
        forest.root.emplace(start, start);

        std::vector<DfsFrame> pending;
        pending.push_back({start, sortedActiveNeighbors(graph, start), 0});
        while (!pending.empty()) {
            DfsFrame& frame = pending.back();
            if (frame.nextNeighbor >= frame.neighbors.size()) {
                pending.pop_back();
                continue;
            }
            const NodeId neighbor = frame.neighbors[frame.nextNeighbor];
            ++frame.nextNeighbor;
            if (forest.parent.count(neighbor) != 0) {
                continue;
            }
            forest.parent.emplace(neighbor, frame.node);
            forest.root.emplace(neighbor, start);
            forest.children[frame.node].push_back(neighbor);
            pending.push_back({neighbor, sortedActiveNeighbors(graph, neighbor), 0});
        }
    }
    return forest;
}

bool isTreeRoot(const DfsForest& forest, NodeId node) {
    return forest.parent.count(node) == 0 || forest.parent.at(node) == node;
}

std::vector<NodeId> routeInForest(const DfsForest& forest, NodeId src, NodeId dst) {
    if (forest.root.count(src) == 0 || forest.root.count(dst) == 0 ||
        forest.root.at(src) != forest.root.at(dst)) {
        return {};
    }

    std::vector<NodeId> path;
    std::unordered_map<NodeId, std::size_t> srcAncestorIndex;
    NodeId current = src;
    while (true) {
        srcAncestorIndex.emplace(current, path.size());
        path.push_back(current);
        if (isTreeRoot(forest, current)) {
            break;
        }
        current = forest.parent.at(current);
    }

    std::vector<NodeId> dstAncestorsBelowCommon;
    current = dst;
    while (srcAncestorIndex.count(current) == 0) {
        dstAncestorsBelowCommon.push_back(current);
        if (isTreeRoot(forest, current)) {
            return {};
        }
        current = forest.parent.at(current);
    }

    path.resize(srcAncestorIndex.at(current) + 1);
    path.insert(path.end(), dstAncestorsBelowCommon.rbegin(), dstAncestorsBelowCommon.rend());
    return path;
}

}

std::vector<NodeId> DFSRouting::computeRoute(const Graph& graph, NodeId src, NodeId dst,
                                             const WeightPolicy&) const {
    return routeInForest(buildDfsForest(graph), src, dst);
}

std::unordered_map<NodeId, NodeId> DFSRouting::computeTable(const Graph& graph, NodeId src,
                                                            const WeightPolicy&) const {
    std::unordered_map<NodeId, NodeId> nextHopByDestination;
    const DfsForest forest = buildDfsForest(graph);
    if (forest.parent.count(src) == 0) {
        return nextHopByDestination;
    }

    std::vector<std::pair<NodeId, NodeId>> pending;
    if (forest.children.count(src) != 0) {
        for (const NodeId child : forest.children.at(src)) {
            pending.emplace_back(child, child);
        }
    }
    while (!pending.empty()) {
        const auto [current, firstHop] = pending.back();
        pending.pop_back();
        nextHopByDestination.emplace(current, firstHop);
        if (forest.children.count(current) == 0) {
            continue;
        }
        for (const NodeId child : forest.children.at(current)) {
            pending.emplace_back(child, firstHop);
        }
    }

    const NodeId sourceParent = forest.parent.at(src);
    if (sourceParent == src) {
        return nextHopByDestination;
    }
    const NodeId sourceRoot = forest.root.at(src);
    for (const auto& [destination, destinationRoot] : forest.root) {
        if (destination != src && destinationRoot == sourceRoot) {
            nextHopByDestination.emplace(destination, sourceParent);
        }
    }
    return nextHopByDestination;
}

std::string DFSRouting::name() const {
    return "DFS";
}

}
