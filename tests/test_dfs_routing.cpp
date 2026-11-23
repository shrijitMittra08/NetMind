#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "netmind/graph/TopologyLoader.h"
#include "netmind/routing/BFSRouting.h"
#include "netmind/routing/DFSRouting.h"
#include "netmind/routing/DjikstraRouting.h"
#include "netmind/routing/WeightPolicy.h"

#ifndef NETMIND_SOURCE_DIR
#error "NETMIND_SOURCE_DIR must be defined by CMake"
#endif

namespace {

netmind::Graph loadSample(const std::string& filename) {
    const auto path =
        std::filesystem::path(NETMIND_SOURCE_DIR) / "config" / "topologies" / filename;
    return netmind::TopologyLoader::loadFromFile(path.string());
}

}

// Checks that DFS always returns the same expected path on the weighted diamond topology
TEST_CASE("DFSRouting returns a deterministic path on the weighted diamond") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::DFSRouting routing;
    netmind::HopCountWeight policy;

    REQUIRE(routing.computeRoute(graph, 1, 8, policy) ==
            std::vector<netmind::NodeId>{1, 2, 3, 4, 5, 7, 8});
}

// Checks that DFS and Dijkstra pick the same route here while BFS picks a different one
TEST_CASE("DFS and Dijkstra match on this sample while BFS chooses differently") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::DFSRouting dfs;
    netmind::DijkstraRouting dijkstra;
    netmind::BFSRouting bfs;
    netmind::LatencyWeight latency;
    netmind::HopCountWeight hops;

    const auto dfsRoute = dfs.computeRoute(graph, 1, 8, latency);
    const auto dijkstraRoute = dijkstra.computeRoute(graph, 1, 8, latency);
    const auto bfsRoute = bfs.computeRoute(graph, 1, 8, hops);

    REQUIRE(dfsRoute == dijkstraRoute);
    REQUIRE(dfsRoute != bfsRoute);
}

// Checks that the DFS table gives the right next hop for each reachable node
TEST_CASE("DFSRouting builds a next-hop table for reachable nodes") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::DFSRouting routing;
    netmind::HopCountWeight policy;

    const auto table = routing.computeTable(graph, 1, policy);

    REQUIRE(table.size() == 3);
    REQUIRE(table.find(1) == table.end());
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.at(3) == 2);
    REQUIRE(table.at(4) == 2);
}

// Checks that DFS finds no route to nodes cut off by a removed link
TEST_CASE("DFSRouting omits destinations unreachable after link removal") {
    netmind::Graph graph = loadSample("small4.json");
    REQUIRE(graph.removeLink(2));

    netmind::DFSRouting routing;
    netmind::HopCountWeight policy;

    REQUIRE(routing.computeRoute(graph, 1, 4, policy).empty());

    const auto table = routing.computeTable(graph, 1, policy);
    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

// Checks that DFS skips a node that is marked down
TEST_CASE("DFSRouting ignores down nodes") {
    netmind::Graph graph = loadSample("small4.json");
    auto* node = graph.getNode(3);
    REQUIRE(node != nullptr);
    node->setUp(false);

    netmind::DFSRouting routing;
    netmind::HopCountWeight policy;

    const auto table = routing.computeTable(graph, 1, policy);

    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

// Checks that DFS returns just the node when source equals destination and nothing for a missing node
TEST_CASE("DFSRouting handles same-node and missing-node routes") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::DFSRouting routing;
    netmind::HopCountWeight policy;

    REQUIRE(routing.computeRoute(graph, 2, 2, policy) == std::vector<netmind::NodeId>{2});
    REQUIRE(routing.computeRoute(graph, 2, 99, policy).empty());
}

// Checks that DFS reports its name as "DFS"
TEST_CASE("DFSRouting identifies itself by name") {
    netmind::DFSRouting routing;
    REQUIRE(routing.name() == "DFS");
}