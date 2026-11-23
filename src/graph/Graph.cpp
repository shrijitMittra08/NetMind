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

std::vector<NodeId> sortedActiveNeighbourIds(const Graph& graph, NodeId id) {
    std::vector<NodeId> neighbourIds;
    for (const Link* link : graph.getActiveNeighbours(id)) {
        neighbourIds.push_back(link->other(id));
    }
    std::sort(neighbourIds.begin(), neighbourIds.end());
    return neighbourIds;
}

bool bothNodesUp(const Graph& graph, NodeId src, NodeId dst) {
    const Node* source = graph.getNode(src);
    const Node* destination = graph.getNode(dst);
    return source != nullptr && destination != nullptr && source->isUp() && destination->isUp();
}

bool reconstructPath(NodeId src, NodeId dst, const std::unordered_map<NodeId, NodeId>& parent,
                     std::vector<NodeId>& outPath) {
    outPath.clear();
    NodeId current = dst;
    outPath.push_back(current);
    while (current != src) {
        if (parent.count(current) == 0) {
            outPath.clear();
            return false;
        }
        current = parent.at(current);
        outPath.push_back(current);
    }
    std::reverse(outPath.begin(), outPath.end());
    return true;
}

void removeFromList(std::vector<Link*>& links, const Link* linkToRemove) {
    links.erase(std::remove(links.begin(), links.end(), linkToRemove), links.end());
}

}

void Graph::addNode(std::unique_ptr<Node> node) {
    if (!node) {
        throw TopologyError("Cannot add a null node");
    }
    const NodeId id = node->getId();
    const std::string& name = node->name();
    if (nodes_.count(id) > 0) {
        throw TopologyError("Duplicate node id: " + std::to_string(id));
    }
    if (nameIndex_.count(name) > 0) {
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
    if (links_.count(linkId) > 0) {
        throw TopologyError("Duplicate link id: " + std::to_string(linkId));
    }
    if (endpointA == endpointB) {
        throw TopologyError("A link cannot connect a node to itself");
    }
    if (nodes_.count(endpointA) == 0 || nodes_.count(endpointB) == 0) {
        throw TopologyError("Link endpoint does not exist in the graph");
    }
    if (linkBetween(endpointA, endpointB) != nullptr) {
        throw TopologyError("Parallel links between the same nodes are not supported");
    }
    Link* linkPointer = link.get();
    links_.emplace(linkId, std::move(link));
    adjacency_.at(endpointA).push_back(linkPointer);
    adjacency_.at(endpointB).push_back(linkPointer);
}

bool Graph::removeLink(LinkId id) {
    if (links_.count(id) == 0) {
        return false;
    }
    const Link* link = links_.at(id).get();
    removeFromList(adjacency_.at(link->a()), link);
    removeFromList(adjacency_.at(link->b()), link);
    links_.erase(id);
    return true;
}

Node* Graph::getNode(NodeId id) {
    return nodes_.count(id) == 0 ? nullptr : nodes_.at(id).get();
}

const Node* Graph::getNode(NodeId id) const {
    return nodes_.count(id) == 0 ? nullptr : nodes_.at(id).get();
}

Link* Graph::getLink(LinkId id) {
    return links_.count(id) == 0 ? nullptr : links_.at(id).get();
}

const Link* Graph::getLink(LinkId id) const {
    return links_.count(id) == 0 ? nullptr : links_.at(id).get();
}

Node* Graph::findByName(const std::string& name) {
    return nameIndex_.count(name) == 0 ? nullptr : getNode(nameIndex_.at(name));
}

const Node* Graph::findByName(const std::string& name) const {
    return nameIndex_.count(name) == 0 ? nullptr : getNode(nameIndex_.at(name));
}

std::vector<const Link*> Graph::getNeighbours(NodeId id) const {
    if (adjacency_.count(id) == 0) {
        return {};
    }
    const std::vector<Link*>& links = adjacency_.at(id);
    return std::vector<const Link*>(links.begin(), links.end());
}

std::vector<const Link*> Graph::getActiveNeighbours(NodeId id) const {
    const Node* node = getNode(id);
    if (adjacency_.count(id) == 0 || node == nullptr || !node->isUp()) {
        return {};
    }
    std::vector<const Link*> activeLinks;
    for (const Link* link : adjacency_.at(id)) {
        const Node* otherNode = getNode(link->other(id));
        if (link->isUsable() && otherNode != nullptr && otherNode->isUp()) {
            activeLinks.push_back(link);
        }
    }
    return activeLinks;
}

const Link* Graph::linkBetween(NodeId a, NodeId b) const {
    if (adjacency_.count(a) == 0) {
        return nullptr;
    }
    for (const Link* link : adjacency_.at(a)) {
        if (link->connects(a, b)) {
            return link;
        }
    }
    return nullptr;
}

std::vector<NodeId> Graph::nodeIds() const {
    std::vector<NodeId> ids;
    for (const auto& entry : nodes_) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool Graph::bfsPath(NodeId src, NodeId dst, std::vector<NodeId>& outPath) const {
    outPath.clear();
    if (!bothNodesUp(*this, src, dst)) {
        return false;
    }
    if (src == dst) {
        outPath.push_back(src);
        return true;
    }
    std::queue<NodeId> toVisit;
    std::unordered_map<NodeId, NodeId> parent;
    std::unordered_set<NodeId> visited{src};
    toVisit.push(src);
    while (!toVisit.empty()) {
        const NodeId current = toVisit.front();
        toVisit.pop();
        for (const NodeId neighbour : sortedActiveNeighbourIds(*this, current)) {
            if (!visited.insert(neighbour).second) {
                continue;
            }
            parent.emplace(neighbour, current);
            if (neighbour == dst) {
                return reconstructPath(src, dst, parent, outPath);
            }
            toVisit.push(neighbour);
        }
    }
    return false;
}

bool Graph::dfsPath(NodeId src, NodeId dst, std::vector<NodeId>& outPath) const {
    outPath.clear();
    if (!bothNodesUp(*this, src, dst)) {
        return false;
    }
    if (src == dst) {
        outPath.push_back(src);
        return true;
    }
    std::stack<NodeId> toVisit;
    std::unordered_map<NodeId, NodeId> parent;
    std::unordered_set<NodeId> visited{src};
    toVisit.push(src);
    while (!toVisit.empty()) {
        const NodeId current = toVisit.top();
        toVisit.pop();
        const std::vector<NodeId> neighbourIds = sortedActiveNeighbourIds(*this, current);
        for (std::size_t i = neighbourIds.size(); i > 0; --i) {
            const NodeId neighbour = neighbourIds[i - 1];
            if (!visited.insert(neighbour).second) {
                continue;
            }
            parent.emplace(neighbour, current);
            if (neighbour == dst) {
                return reconstructPath(src, dst, parent, outPath);
            }
            toVisit.push(neighbour);
        }
    }
    return false;
}

std::vector<std::vector<NodeId>> Graph::connectedComponents() const {
    const std::vector<NodeId> ids = nodeIds();
    std::unordered_map<NodeId, std::size_t> indexById;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        indexById.emplace(ids[index], index);
    }
    UnionFind groups(ids.size());
    for (const NodeId id : ids) {
        for (const Link* link : getActiveNeighbours(id)) {
            groups.unite(indexById.at(id), indexById.at(link->other(id)));
        }
    }
    std::unordered_map<std::size_t, std::size_t> componentIndexByRoot;
    std::vector<std::vector<NodeId>> components;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const std::size_t root = groups.find(index);
        if (componentIndexByRoot.count(root) == 0) {
            componentIndexByRoot.emplace(root, components.size());
            components.emplace_back();
        }
        components[componentIndexByRoot.at(root)].push_back(ids[index]);
    }
    return components;
}

bool Graph::hasCycle() const {
    std::unordered_set<NodeId> visited;
    std::unordered_map<NodeId, NodeId> parent;
    for (const NodeId start : nodeIds()) {
        if (!getNode(start)->isUp() || visited.count(start) > 0) {
            continue;
        }
        std::stack<NodeId> toVisit;
        toVisit.push(start);
        visited.insert(start);
        parent.emplace(start, start);
        while (!toVisit.empty()) {
            const NodeId current = toVisit.top();
            toVisit.pop();
            for (const NodeId neighbour : sortedActiveNeighbourIds(*this, current)) {
                if (visited.count(neighbour) == 0) {
                    visited.insert(neighbour);
                    parent.emplace(neighbour, current);
                    toVisit.push(neighbour);
                } else if (parent.at(current) != neighbour) {
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
    std::queue<NodeId> toVisit;
    std::unordered_set<NodeId> visited{src};
    toVisit.push(src);
    while (!toVisit.empty()) {
        const NodeId current = toVisit.front();
        toVisit.pop();
        for (const NodeId neighbour : sortedActiveNeighbourIds(*this, current)) {
            if (visited.insert(neighbour).second) {
                toVisit.push(neighbour);
            }
        }
    }
    std::vector<NodeId> reachable(visited.begin(), visited.end());
    std::sort(reachable.begin(), reachable.end());
    return reachable;
}

}
