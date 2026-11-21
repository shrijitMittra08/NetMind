#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "netmind/graph/TopologyLoader.h"
#include "netmind/routing/BFSRouting.h"
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

TEST_CASE("BFSRouting returns a fewest-hop route") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::BFSRouting routing;
    netmind::LatencyWeight latencyPolicy;

    const auto route = routing.computeRoute(graph, 1, 8, latencyPolicy);

    REQUIRE(route == std::vector<netmind::NodeId>{1, 2, 6, 7, 8});
}

TEST_CASE("BFSRouting returns an empty route when destination is unreachable") {
    netmind::Graph graph = loadSample("small4.json");
    REQUIRE(graph.removeLink(2));

    netmind::BFSRouting routing;
    netmind::HopCountWeight hopPolicy;

    REQUIRE(routing.computeRoute(graph, 1, 4, hopPolicy).empty());
}

TEST_CASE("BFSRouting returns a one-node path when source equals destination") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::BFSRouting routing;
    netmind::HopCountWeight hopPolicy;

    REQUIRE(routing.computeRoute(graph, 2, 2, hopPolicy) == std::vector<netmind::NodeId>{2});
}

TEST_CASE("BFSRouting computeTable gives each destination its first hop") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::BFSRouting routing;
    netmind::HopCountWeight hopPolicy;

    const auto table = routing.computeTable(graph, 1, hopPolicy);

    REQUIRE(table.size() == 3);
    REQUIRE(table.find(1) == table.end());
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.at(3) == 2);
    REQUIRE(table.at(4) == 2);
}

TEST_CASE("BFSRouting table omits destinations beyond a removed link") {
    netmind::Graph graph = loadSample("small4.json");
    REQUIRE(graph.removeLink(2));

    netmind::BFSRouting routing;
    netmind::HopCountWeight hopPolicy;

    const auto table = routing.computeTable(graph, 1, hopPolicy);

    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

TEST_CASE("BFSRouting ignores down links and down nodes") {
    netmind::Graph graph = loadSample("small4.json");
    netmind::BFSRouting routing;
    netmind::HopCountWeight hopPolicy;

    auto* router = graph.getNode(3);
    REQUIRE(router != nullptr);
    router->setUp(false);

    const auto table = routing.computeTable(graph, 1, hopPolicy);

    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

TEST_CASE("BFSRouting uses node id order to break equal-hop ties") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::BFSRouting routing;
    netmind::HopCountWeight hopPolicy;

    const auto table = routing.computeTable(graph, 1, hopPolicy);

    REQUIRE(table.at(8) == 2);
    REQUIRE(table.at(7) == 2);
}

TEST_CASE("BFSRouting identifies itself by name") {
    netmind::BFSRouting routing;
    REQUIRE(routing.name() == "BFS");
}