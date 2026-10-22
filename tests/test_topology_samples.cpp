#include "netmind/common/Errors.h"
#include "netmind/graph/TopologyLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#ifndef NETMIND_SOURCE_DIR
#error "NETMIND_SOURCE_DIR must be defined by CMake"
#endif

namespace {

std::filesystem::path projectRoot() {
    return std::filesystem::path(NETMIND_SOURCE_DIR);
}

netmind::Graph loadSample(const std::string& filename) {
    const auto path =
        projectRoot() / "config" / "topologies" / filename;
    return netmind::TopologyLoader::loadFromFile(path.string());
}

}             

TEST_CASE("small4 topology loads and has the expected BFS path") {
    const netmind::Graph graph = loadSample("small4.json");
    std::vector<netmind::NodeId> path;

    REQUIRE(graph.isConnected());
    REQUIRE(graph.bfsPath(1, 4, path));
    REQUIRE(path == std::vector<netmind::NodeId>{1, 2, 3, 4});
}

TEST_CASE("ring_backup provides an alternate path after a link is removed") {
    netmind::Graph graph = loadSample("ring_backup.json");
    std::vector<netmind::NodeId> path;

    REQUIRE(graph.bfsPath(1, 6, path));
    REQUIRE(path == std::vector<netmind::NodeId>{1, 2, 3, 4, 6});

                                                                     
    REQUIRE(graph.removeLink(2));
    REQUIRE(graph.bfsPath(1, 6, path));
    REQUIRE(path == std::vector<netmind::NodeId>{1, 2, 5, 4, 6});
}

TEST_CASE("diamond_weighted makes BFS and DFS choose different paths") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    std::vector<netmind::NodeId> bfsPath;
    std::vector<netmind::NodeId> dfsPath;

    REQUIRE(graph.isConnected());

    REQUIRE(graph.bfsPath(1, 8, bfsPath));
    REQUIRE(bfsPath == std::vector<netmind::NodeId>{1, 2, 6, 7, 8});

    REQUIRE(graph.dfsPath(1, 8, dfsPath));
    REQUIRE(dfsPath == std::vector<netmind::NodeId>{1, 2, 3, 4, 5, 7, 8});

    REQUIRE(bfsPath != dfsPath);
    REQUIRE(graph.linkBetween(2, 6)->latencyMs() == 40.0);
    REQUIRE(graph.linkBetween(2, 3)->latencyMs() == 2.0);
}

TEST_CASE("dumbbell topology loads as a connected network") {
    const netmind::Graph graph = loadSample("dumbbell.json");

    REQUIRE(graph.numNodes() == 6);
    REQUIRE(graph.numLinks() == 5);
    REQUIRE(graph.isConnected());
    REQUIRE(graph.linkBetween(3, 4)->bandwidthMbps() == 50.0);
}

TEST_CASE("medium10 topology is connected and contains cycles") {
    const netmind::Graph graph = loadSample("medium10.json");

    REQUIRE(graph.numNodes() == 10);
    REQUIRE(graph.isConnected());
    REQUIRE(graph.hasCycle());
}

TEST_CASE("mini topology fixture loads from a file") {
    const auto path =
        projectRoot() / "tests" / "golden" / "mini_topology.json";
    const netmind::Graph graph =
        netmind::TopologyLoader::loadFromFile(path.string());

    REQUIRE(graph.numNodes() == 4);
    REQUIRE(graph.isConnected());
}

TEST_CASE("loader rejects invalid topology fixtures") {
    const std::vector<std::string> invalidFiles{
        "bad_duplicate_ip.json",
        "bad_unknown_node.json",
        "bad_host_links.json",
        "bad_link_values.json"
    };

    for (const std::string& filename : invalidFiles) {
        const auto path =
            projectRoot() / "tests" / "golden" / filename;

        INFO("Invalid topology fixture: " << filename);
        REQUIRE_THROWS_AS(
            netmind::TopologyLoader::loadFromFile(path.string()),
            netmind::TopologyError
        );
    }
}