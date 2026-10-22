#include "netmind/graph/Graph.h"

#include "netmind/common/Errors.h"
#include "netmind/common/UnionFind.h"

#include <algorithm>
#include <queue>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace netmind {
namespace {

std::vector<NodeId> sortedActiveNeighborIds(
    const Graph& graph,
    NodeId id
) {
    std::vector<NodeId> neighbors;

    for (const Link* link : graph.getActiveNeighbours(id)) {
        neighbors.push_back(link->other(id));
    }

    std::sort(neighbors.begin(), neighbors.end());
    return neighbors;
}

bool reconstructPath(
    NodeId src,
    NodeId dst,
    const std::unordered_map<NodeId, NodeId>& parent,
    std::vector<NodeId>& outPath
) {
    outPath.clear();

    auto current = dst;
    outPath.push_back(current);

    while (current != src) {
        const auto parentIt = parent.find(current);
        if (parentIt == parent.end()) {
            outPath.clear();
            return false;
        }

        current = parentIt->second;
        outPath.push_back(current);
    }

    std::reverse(outPath.begin(), outPath.end());
    return true;
}

}             

void Graph::addNode(std::unique_ptr<Node> node) {
    if (!node) {
        throw TopologyError("Cannot add a null node");
    }

    const NodeId id = node->getId();
    const std::string& name = node->name();

    if (nodes_.find(id) != nodes_.end()) {
        throw TopologyError("Duplicate node id: " + std::to_string(id));
    }

    if (nameIndex_.find(name) != nameIndex_.end()) {
        throw TopologyError("Duplicate node name: " + name);
    }

    adjacency_.try_emplace(id);
    nameIndex_.emplace(name, id);
    nodes_.emplace(id, std::move(node));
}

void Graph::addLink(std::unique_ptr<Link> link) {
    if (!link) {
        throw TopologyError("Cannot add a null link");
    }

    const LinkId linkId = link->getId();
    const NodeId endpointA = link->a();
    const NodeId endpointB = link->b();

    if (links_.find(linkId) != links_.end()) {
        throw TopologyError(
            "Duplicate link id: " + std::to_string(linkId));
    }

    if (endpointA == endpointB) {
        throw TopologyError("A link cannot connect a node to itself");
    }

    if (nodes_.find(endpointA) == nodes_.end() ||
        nodes_.find(endpointB) == nodes_.end()) {
        throw TopologyError("Link endpoint does not exist in the graph");
    }

    const auto& adjacentToA = adjacency_.at(endpointA);
    for (const Link* existing : adjacentToA) {
        if (existing->connects(endpointA, endpointB)) {
            throw TopologyError(
                "Parallel links between the same nodes are not supported");
        }
    }

    Link* linkPointer = link.get();
    links_.emplace(linkId, std::move(link));
    adjacency_.at(endpointA).push_back(linkPointer);
    adjacency_.at(endpointB).push_back(linkPointer);
}

bool Graph::removeLink(LinkId id) {
    const auto linkIt = links_.find(id);
    if (linkIt == links_.end()) {
        return false;
    }

    Link* link = linkIt->second.get();

    auto& linksAtA = adjacency_.at(link->a());
    linksAtA.erase(
        std::remove(linksAtA.begin(), linksAtA.end(), link),
        linksAtA.end());

    auto& linksAtB = adjacency_.at(link->b());
    linksAtB.erase(
        std::remove(linksAtB.begin(), linksAtB.end(), link),
        linksAtB.end());

    links_.erase(linkIt);
    return true;
}

Node* Graph::getNode(NodeId id) {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}

const Node* Graph::getNode(NodeId id) const {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}

Link* Graph::getLink(LinkId id) {
    const auto it = links_.find(id);
    return it == links_.end() ? nullptr : it->second.get();
}

const Link* Graph::getLink(LinkId id) const {
    const auto it = links_.find(id);
    return it == links_.end() ? nullptr : it->second.get();
}

Node* Graph::findByName(const std::string& name) {
    const auto nameIt = nameIndex_.find(name);
    if (nameIt == nameIndex_.end()) {
        return nullptr;
    }

    return getNode(nameIt->second);
}

const Node* Graph::findByName(const std::string& name) const {
    const auto nameIt = nameIndex_.find(name);
    if (nameIt == nameIndex_.end()) {
        return nullptr;
    }

    return getNode(nameIt->second);
}

std::vector<const Link*> Graph::getNeighbours(NodeId id) const {
    const auto adjacencyIt = adjacency_.find(id);
    if (adjacencyIt == adjacency_.end()) {
        return {};
    }

    std::vector<const Link*> result;
    result.reserve(adjacencyIt->second.size());

    for (const Link* link : adjacencyIt->second) {
        result.push_back(link);
    }

    return result;
}

std::vector<const Link*> Graph::getActiveNeighbours(NodeId id) const {
    const auto adjacencyIt = adjacency_.find(id);
    if (adjacencyIt == adjacency_.end()) {
        return {};
    }

    const Node* node = getNode(id);
    if (node == nullptr || !node->isUp()) {
        return {};
    }

    std::vector<const Link*> result;
    result.reserve(adjacencyIt->second.size());

    for (const Link* link : adjacencyIt->second) {
        const Node* otherNode = getNode(link->other(id));

        if (link->isUsable() &&
            otherNode != nullptr &&
            otherNode->isUp()) {
            result.push_back(link);
        }
    }

    return result;
}

const Link* Graph::linkBetween(NodeId a, NodeId b) const {
    const auto adjacencyIt = adjacency_.find(a);
    if (adjacencyIt == adjacency_.end()) {
        return nullptr;
    }

    for (const Link* link : adjacencyIt->second) {
        if (link->connects(a, b)) {
            return link;
        }
    }

    return nullptr;
}

std::vector<NodeId> Graph::nodeIds() const {
    std::vector<NodeId> result;
    result.reserve(nodes_.size());

    for (const auto& entry : nodes_) {
        result.push_back(entry.first);
    }

    std::sort(result.begin(), result.end());
    return result;
}

bool Graph::bfsPath(
    NodeId src,
    NodeId dst,
    std::vector<NodeId>& outPath
) const {
    outPath.clear();

    const Node* source = getNode(src);
    const Node* destination = getNode(dst);

    if (source == nullptr ||
        destination == nullptr ||
        !source->isUp() ||
        !destination->isUp()) {
        return false;
    }

    if (src == dst) {
        outPath.push_back(src);
        return true;
    }

    std::queue<NodeId> pending;
    std::unordered_map<NodeId, NodeId> parent;
    std::unordered_set<NodeId> visited;

    pending.push(src);
    visited.insert(src);

    while (!pending.empty()) {
        const NodeId current = pending.front();
        pending.pop();

        for (const NodeId neighbor : sortedActiveNeighborIds(*this, current)) {
            if (!visited.insert(neighbor).second) {
                continue;
            }

            parent.emplace(neighbor, current);

            if (neighbor == dst) {
                return reconstructPath(src, dst, parent, outPath);
            }

            pending.push(neighbor);
        }
    }

    return false;
}

bool Graph::dfsPath(
    NodeId src,
    NodeId dst,
    std::vector<NodeId>& outPath
) const {
    outPath.clear();

    const Node* source = getNode(src);
    const Node* destination = getNode(dst);

    if (source == nullptr ||
        destination == nullptr ||
        !source->isUp() ||
        !destination->isUp()) {
        return false;
    }

    if (src == dst) {
        outPath.push_back(src);
        return true;
    }

    std::stack<NodeId> pending;
    std::unordered_map<NodeId, NodeId> parent;
    std::unordered_set<NodeId> visited;

    pending.push(src);
    visited.insert(src);

    while (!pending.empty()) {
        const NodeId current = pending.top();
        pending.pop();

        const auto neighbors = sortedActiveNeighborIds(*this, current);

                                                                  
        for (auto it = neighbors.rbegin(); it != neighbors.rend(); ++it) {
            const NodeId neighbor = *it;
            if (!visited.insert(neighbor).second) {
                continue;
            }

            parent.emplace(neighbor, current);

            if (neighbor == dst) {
                return reconstructPath(src, dst, parent, outPath);
            }

            pending.push(neighbor);
        }
    }

    return false;
}

std::vector<std::vector<NodeId>> Graph::connectedComponents() const {
    const std::vector<NodeId> ids = nodeIds();

    std::unordered_map<NodeId, std::size_t> indexById;
    indexById.reserve(ids.size());

    for (std::size_t index = 0; index < ids.size(); ++index) {
        indexById.emplace(ids[index], index);
    }

    UnionFind sets(ids.size());

    for (const NodeId id : ids) {
        const std::size_t idIndex = indexById.at(id);

        for (const Link* link : getActiveNeighbours(id)) {
            const NodeId neighbor = link->other(id);
            sets.unite(idIndex, indexById.at(neighbor));
        }
    }

    std::unordered_map<std::size_t, std::size_t> groupIndexByRoot;
    std::vector<std::vector<NodeId>> components;

    for (std::size_t index = 0; index < ids.size(); ++index) {
        const std::size_t root = sets.find(index);
        auto groupIt = groupIndexByRoot.find(root);

        if (groupIt == groupIndexByRoot.end()) {
            const std::size_t groupIndex = components.size();
            groupIt = groupIndexByRoot.emplace(root, groupIndex).first;
            components.emplace_back();
        }

        components[groupIt->second].push_back(ids[index]);
    }

    return components;
}

bool Graph::isConnected() const {
    return connectedComponents().size() == 1;
}

bool Graph::hasCycle() const {
    std::unordered_set<NodeId> visited;
    std::unordered_map<NodeId, NodeId> parent;

    for (const NodeId start : nodeIds()) {
        const Node* startNode = getNode(start);
        if (startNode == nullptr ||
            !startNode->isUp() ||
            visited.find(start) != visited.end()) {
            continue;
        }

        std::stack<NodeId> pending;
        pending.push(start);
        visited.insert(start);
        parent.emplace(start, start);

        while (!pending.empty()) {
            const NodeId current = pending.top();
            pending.pop();

            for (const NodeId neighbor :
                 sortedActiveNeighborIds(*this, current)) {
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    parent.emplace(neighbor, current);
                    pending.push(neighbor);
                } else if (parent.at(current) != neighbor) {
                    return true;
                }
            }
        }
    }

    return false;
}

std::vector<NodeId> Graph::reachableFrom(NodeId src) const {
    const Node* source = getNode(src);
    if (source == nullptr || !source->isUp()) {
        return {};
    }

    std::queue<NodeId> pending;
    std::unordered_set<NodeId> visited;

    pending.push(src);
    visited.insert(src);

    while (!pending.empty()) {
        const NodeId current = pending.front();
        pending.pop();

        for (const NodeId neighbor : sortedActiveNeighborIds(*this, current)) {
            if (visited.insert(neighbor).second) {
                pending.push(neighbor);
            }
        }
    }

    std::vector<NodeId> result(visited.begin(), visited.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::size_t Graph::numNodes() const {
    return nodes_.size();
}

std::size_t Graph::numLinks() const {
    return links_.size();
}

}