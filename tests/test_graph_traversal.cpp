#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace {

void addNode(
    netmind::Graph& graph,
    netmind::NodeId id,
    const char* name
) {
    graph.addNode(std::make_unique<netmind::Router>(
        id,
        name,
        "10.0.0." + std::to_string(id)));
}

void addLink(
    netmind::Graph& graph,
    netmind::LinkId id,
    netmind::NodeId a,
    netmind::NodeId b
) {
    graph.addLink(std::make_unique<netmind::Link>(
        id, a, b, 100.0, 1.0, 0.0, 16));
}

netmind::Graph makeDiamondGraph() {
    netmind::Graph graph;
    addNode(graph, 1, "N1");
    addNode(graph, 2, "N2");
    addNode(graph, 3, "N3");
    addNode(graph, 4, "N4");
    addNode(graph, 5, "N5");

    addLink(graph, 10, 1, 2);
    addLink(graph, 11, 1, 3);
    addLink(graph, 12, 2, 4);
    addLink(graph, 13, 3, 4);
    addLink(graph, 14, 4, 5);

    return graph;
}

}             

TEST_CASE("Graph BFS returns a deterministic shortest-hop path") {
    netmind::Graph graph = makeDiamondGraph();
    std::vector<netmind::NodeId> path;

    REQUIRE(graph.bfsPath(1, 5, path));
    REQUIRE(path == std::vector<netmind::NodeId>{1, 2, 4, 5});
}

TEST_CASE("Graph DFS returns a deterministic depth-first path") {
    netmind::Graph graph = makeDiamondGraph();
    std::vector<netmind::NodeId> path;

    REQUIRE(graph.dfsPath(1, 5, path));
    REQUIRE(path == std::vector<netmind::NodeId>{1, 2, 4, 5});
}

TEST_CASE("Graph path searches handle same, missing, and disconnected nodes") {
    netmind::Graph graph;
    addNode(graph, 1, "N1");
    addNode(graph, 2, "N2");

    std::vector<netmind::NodeId> path{99};

    REQUIRE(graph.bfsPath(1, 1, path));
    REQUIRE(path == std::vector<netmind::NodeId>{1});

    REQUIRE(graph.dfsPath(1, 1, path));
    REQUIRE(path == std::vector<netmind::NodeId>{1});

    REQUIRE_FALSE(graph.bfsPath(1, 2, path));
    REQUIRE(path.empty());

    REQUIRE_FALSE(graph.dfsPath(1, 99, path));
    REQUIRE(path.empty());
}

TEST_CASE("Graph path searches ignore down nodes and links") {
    netmind::Graph graph;
    addNode(graph, 1, "N1");
    addNode(graph, 2, "N2");
    addNode(graph, 3, "N3");
    addLink(graph, 10, 1, 2);
    addLink(graph, 11, 2, 3);

    std::vector<netmind::NodeId> path;
    REQUIRE(graph.bfsPath(1, 3, path));

    graph.getNode(2)->setUp(false);
    REQUIRE_FALSE(graph.bfsPath(1, 3, path));
    REQUIRE(path.empty());

    graph.getNode(2)->setUp(true);
    auto* link = const_cast<netmind::Link*>(graph.linkBetween(2, 3));
    REQUIRE(link != nullptr);
    link->setState(netmind::LinkState::DOWN);

    REQUIRE_FALSE(graph.dfsPath(1, 3, path));
}

TEST_CASE("Graph finds active connected components") {
    netmind::Graph graph;
    addNode(graph, 1, "N1");
    addNode(graph, 2, "N2");
    addNode(graph, 3, "N3");
    addNode(graph, 4, "N4");
    addNode(graph, 5, "N5");

    addLink(graph, 10, 1, 2);
    addLink(graph, 11, 2, 3);
    addLink(graph, 12, 4, 5);

    const auto components = graph.connectedComponents();

    REQUIRE(components.size() == 2);
    REQUIRE(components[0] == std::vector<netmind::NodeId>{1, 2, 3});
    REQUIRE(components[1] == std::vector<netmind::NodeId>{4, 5});
    REQUIRE_FALSE(graph.isConnected());
}

TEST_CASE("Graph connectivity updates after link failure or removal") {
    netmind::Graph graph;
    addNode(graph, 1, "N1");
    addNode(graph, 2, "N2");
    addNode(graph, 3, "N3");
    addLink(graph, 10, 1, 2);
    addLink(graph, 11, 2, 3);

    REQUIRE(graph.isConnected());

    auto* failedLink =
        const_cast<netmind::Link*>(graph.linkBetween(2, 3));
    REQUIRE(failedLink != nullptr);
    failedLink->setState(netmind::LinkState::DOWN);

    REQUIRE_FALSE(graph.isConnected());
    REQUIRE(graph.removeLink(11));
    REQUIRE(graph.connectedComponents().size() == 2);
}

TEST_CASE("Graph detects cycles in an undirected topology") {
    netmind::Graph graph;
    addNode(graph, 1, "N1");
    addNode(graph, 2, "N2");
    addNode(graph, 3, "N3");
    addNode(graph, 4, "N4");

    addLink(graph, 10, 1, 2);
    addLink(graph, 11, 2, 3);
    addLink(graph, 12, 3, 1);

    REQUIRE(graph.hasCycle());

    addLink(graph, 13, 3, 4);
    REQUIRE(graph.hasCycle());
}

TEST_CASE("Graph reports reachable nodes in sorted order") {
    netmind::Graph graph;
    addNode(graph, 1, "N1");
    addNode(graph, 2, "N2");
    addNode(graph, 3, "N3");
    addNode(graph, 4, "N4");
    addLink(graph, 10, 1, 3);
    addLink(graph, 11, 1, 2);

    REQUIRE(
        graph.reachableFrom(1) ==
        std::vector<netmind::NodeId>{1, 2, 3}
    );
    REQUIRE(graph.reachableFrom(4) == std::vector<netmind::NodeId>{4});
    REQUIRE(graph.reachableFrom(99).empty());
}