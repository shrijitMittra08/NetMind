#include "netmind/routing/DFSRouting.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
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

        if (startNode == nullptr || !startNode->isUp() ||
            forest.parent.find(start) != forest.parent.end()) {
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

            const NodeId neighbor = frame.neighbors[frame.nextNeighbor++];

            if (forest.parent.find(neighbor) != forest.parent.end()) {
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

std::vector<NodeId> routeInForest(const DfsForest& forest, NodeId src, NodeId dst) {
    const auto srcRoot = forest.root.find(src);
    const auto dstRoot = forest.root.find(dst);

    if (srcRoot == forest.root.end() || dstRoot == forest.root.end() ||
        srcRoot->second != dstRoot->second) {
        return {};
    }

    std::vector<NodeId> srcAncestors;
    std::unordered_map<NodeId, std::size_t> srcAncestorIndex;

    NodeId current = src;

    while (true) {
        srcAncestorIndex.emplace(current, srcAncestors.size());
        srcAncestors.push_back(current);

        const auto parent = forest.parent.find(current);
        if (parent == forest.parent.end() || parent->second == current) {
            break;
        }

        current = parent->second;
    }

    std::vector<NodeId> dstAncestorsBelowCommon;
    current = dst;

    while (srcAncestorIndex.find(current) == srcAncestorIndex.end()) {
        dstAncestorsBelowCommon.push_back(current);

        const auto parent = forest.parent.find(current);
        if (parent == forest.parent.end() || parent->second == current) {
            return {};
        }

        current = parent->second;
    }

    const std::size_t commonIndex = srcAncestorIndex.at(current);

    std::vector<NodeId> path;
    path.reserve(commonIndex + 1 + dstAncestorsBelowCommon.size());

    for (std::size_t index = 0; index <= commonIndex; ++index) {
        path.push_back(srcAncestors[index]);
    }

    for (auto it = dstAncestorsBelowCommon.rbegin(); it != dstAncestorsBelowCommon.rend(); ++it) {
        path.push_back(*it);
    }

    return path;
}

}

std::vector<NodeId> DFSRouting::computeRoute(const Graph& graph, NodeId src, NodeId dst,
                                             const WeightPolicy&) const {
    const DfsForest forest = buildDfsForest(graph);
    return routeInForest(forest, src, dst);
}

std::unordered_map<NodeId, NodeId> DFSRouting::computeTable(const Graph& graph, NodeId src,
                                                            const WeightPolicy&) const {
    std::unordered_map<NodeId, NodeId> nextHopByDestination;
    const DfsForest forest = buildDfsForest(graph);

    if (forest.parent.find(src) == forest.parent.end()) {
        return nextHopByDestination;
    }

    std::vector<std::pair<NodeId, NodeId>> pending;

    const auto sourceChildren = forest.children.find(src);
    if (sourceChildren != forest.children.end()) {
        for (const NodeId child : sourceChildren->second) {
            pending.emplace_back(child, child);
        }
    }

    while (!pending.empty()) {
        const auto [current, firstHop] = pending.back();
        pending.pop_back();

        nextHopByDestination.emplace(current, firstHop);

        const auto children = forest.children.find(current);
        if (children == forest.children.end()) {
            continue;
        }

        for (const NodeId child : children->second) {
            pending.emplace_back(child, firstHop);
        }
    }

    const NodeId sourceParent = forest.parent.at(src);

    if (sourceParent != src) {
        const NodeId sourceRoot = forest.root.at(src);

        for (const auto& entry : forest.root) {
            const NodeId destination = entry.first;

            if (destination == src || entry.second != sourceRoot ||
                nextHopByDestination.find(destination) != nextHopByDestination.end()) {
                continue;
            }

            nextHopByDestination.emplace(destination, sourceParent);
        }
    }

    return nextHopByDestination;
}

std::string DFSRouting::name() const {
    return "DFS";
}

}