#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "netmind/common/Errors.h"
#include "netmind/graph/TopologyLoader.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/routing/BFSRouting.h"
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

class ConstantWeight final : public netmind::WeightPolicy {
  public:
    explicit ConstantWeight(double value) : value_(value) {}

    double weight(const netmind::Link&) const override {
        return value_;
    }

  private:
    double value_;
};

netmind::Graph makeEqualCostGraph() {
    netmind::Graph graph;

    graph.addNode(std::make_unique<netmind::Host>(1, "PC1", "10.5.0.1"));
    graph.addNode(std::make_unique<netmind::Router>(2, "R1", "10.5.0.2"));
    graph.addNode(std::make_unique<netmind::Router>(3, "R2", "10.5.0.3"));
    graph.addNode(std::make_unique<netmind::Router>(4, "R3", "10.5.0.4"));
    graph.addNode(std::make_unique<netmind::Host>(5, "PC2", "10.5.0.5"));

    graph.addLink(std::make_unique<netmind::Link>(10, 1, 2, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(11, 2, 3, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(12, 2, 4, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(13, 3, 4, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(14, 3, 5, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(15, 4, 5, 100.0, 1.0, 0.0, 16));

    return graph;
}

}

TEST_CASE("Dijkstra chooses the lower total-latency route") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::DijkstraRouting routing;
    netmind::LatencyWeight policy;

    const auto route = routing.computeRoute(graph, 1, 8, policy);

    REQUIRE(route == std::vector<netmind::NodeId>{1, 2, 3, 4, 5, 7, 8});
}

TEST_CASE("Dijkstra and BFS choose different routes on the weighted diamond") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::DijkstraRouting dijkstra;
    netmind::BFSRouting bfs;
    netmind::LatencyWeight latency;
    netmind::HopCountWeight hops;

    const auto weightedRoute = dijkstra.computeRoute(graph, 1, 8, latency);
    const auto fewestHopRoute = bfs.computeRoute(graph, 1, 8, hops);

    REQUIRE(weightedRoute != fewestHopRoute);
    REQUIRE(weightedRoute == std::vector<netmind::NodeId>{1, 2, 3, 4, 5, 7, 8});
    REQUIRE(fewestHopRoute == std::vector<netmind::NodeId>{1, 2, 6, 7, 8});
}

TEST_CASE("Dijkstra computes first-hop entries for every reachable node") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::DijkstraRouting routing;
    netmind::LatencyWeight policy;

    const auto table = routing.computeTable(graph, 1, policy);

    REQUIRE(table.size() == 3);
    REQUIRE(table.find(1) == table.end());
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.at(3) == 2);
    REQUIRE(table.at(4) == 2);
}

TEST_CASE("Dijkstra omits unreachable nodes from its route and table") {
    netmind::Graph graph = loadSample("small4.json");
    REQUIRE(graph.removeLink(2));

    netmind::DijkstraRouting routing;
    netmind::LatencyWeight policy;

    REQUIRE(routing.computeRoute(graph, 1, 4, policy).empty());

    const auto table = routing.computeTable(graph, 1, policy);
    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

TEST_CASE("Dijkstra ignores down nodes and links") {
    netmind::Graph graph = loadSample("small4.json");
    auto* router = graph.getNode(3);
    REQUIRE(router != nullptr);
    router->setUp(false);

    netmind::DijkstraRouting routing;
    netmind::LatencyWeight policy;

    const auto table = routing.computeTable(graph, 1, policy);

    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

TEST_CASE("Dijkstra breaks equal-cost route ties by smaller next-hop id") {
    const netmind::Graph graph = makeEqualCostGraph();
    netmind::DijkstraRouting routing;
    netmind::LatencyWeight policy;

    const auto route = routing.computeRoute(graph, 1, 5, policy);
    REQUIRE(route == std::vector<netmind::NodeId>{1, 2, 3, 5});

    const auto table = routing.computeTable(graph, 1, policy);
    REQUIRE(table.at(5) == 2);
}

TEST_CASE("Dijkstra rejects negative policy weights") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::DijkstraRouting routing;
    ConstantWeight negativeWeight(-1.0);

    REQUIRE_THROWS_AS(routing.computeRoute(graph, 1, 4, negativeWeight), netmind::ConfigError);
}

TEST_CASE("Dijkstra rejects non-finite policy weights") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::DijkstraRouting routing;
    ConstantWeight infiniteWeight(std::numeric_limits<double>::infinity());

    REQUIRE_THROWS_AS(routing.computeRoute(graph, 1, 4, infiniteWeight), netmind::ConfigError);
}

TEST_CASE("Dijkstra identifies itself by name") {
    netmind::DijkstraRouting routing;
    REQUIRE(routing.name() == "Dijkstra");
}