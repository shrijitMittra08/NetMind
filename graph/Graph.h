#pragma once

#include "netmind/model/Link.h"
#include "netmind/model/Node.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace netmind {

class Graph {
public:
    Graph() = default;
    ~Graph() = default;

    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    Graph(Graph&&) = default;
    Graph& operator=(Graph&&) = default;

    void addNode(std::unique_ptr<Node> node);
    void addLink(std::unique_ptr<Link> link);
    bool removeLink(LinkId id);

    [[nodiscard]] Link* getLink(LinkId id);
    [[nodiscard]] const Link* getLink(LinkId id) const;

    [[nodiscard]] Node* getNode(NodeId id);
    [[nodiscard]] const Node* getNode(NodeId id) const;

    [[nodiscard]] Node* findByName(const std::string& name);
    [[nodiscard]] const Node* findByName(const std::string& name) const;

    [[nodiscard]] std::vector<const Link*> getNeighbours(NodeId id) const;
    [[nodiscard]] std::vector<const Link*> getActiveNeighbours(
        NodeId id
    ) const;

    [[nodiscard]] const Link* linkBetween(NodeId a, NodeId b) const;

    [[nodiscard]] std::vector<NodeId> nodeIds() const;

    // Finds a shortest-hop path using active links.
    // On failure, clears outPath and returns false.
    [[nodiscard]] bool bfsPath(
        NodeId src,
        NodeId dst,
        std::vector<NodeId>& outPath
    ) const;

    [[nodiscard]] bool dfsPath(
        NodeId src,
        NodeId dst,
        std::vector<NodeId>& outPath
    ) const;

    [[nodiscard]] std::vector<std::vector<NodeId>>
    connectedComponents() const;

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool hasCycle() const;
    [[nodiscard]] std::vector<NodeId> reachableFrom(NodeId src) const;

    [[nodiscard]] std::size_t numNodes() const;
    [[nodiscard]] std::size_t numLinks() const;

private:
    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes_;
    std::unordered_map<LinkId, std::unique_ptr<Link>> links_;
    std::unordered_map<NodeId, std::vector<Link*>> adjacency_;
    std::unordered_map<std::string, NodeId> nameIndex_;
};

}