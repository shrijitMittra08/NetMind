#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "netmind/common/Errors.h"
#include "netmind/graph/TopologyLoader.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/routing/BellmanFordRouting.h"
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

    graph.addNode(std::make_unique<netmind::Host>(1, "PC1", "10.6.0.1"));
    graph.addNode(std::make_unique<netmind::Router>(2, "R1", "10.6.0.2"));
    graph.addNode(std::make_unique<netmind::Router>(3, "R2", "10.6.0.3"));
    graph.addNode(std::make_unique<netmind::Router>(4, "R3", "10.6.0.4"));
    graph.addNode(std::make_unique<netmind::Host>(5, "PC2", "10.6.0.5"));

    graph.addLink(std::make_unique<netmind::Link>(10, 1, 2, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(11, 2, 3, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(12, 2, 4, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(13, 3, 4, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(14, 3, 5, 100.0, 1.0, 0.0, 16));
    graph.addLink(std::make_unique<netmind::Link>(15, 4, 5, 100.0, 1.0, 0.0, 16));

    return graph;
}

}

TEST_CASE("Bellman-Ford finds the lowest-latency diamond route") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::BellmanFordRouting routing;
    netmind::LatencyWeight policy;

    REQUIRE(routing.computeRoute(graph, 1, 8, policy) ==
            std::vector<netmind::NodeId>{1, 2, 3, 4, 5, 7, 8});
}

TEST_CASE("Bellman-Ford and Dijkstra agree on non-negative weights") {
    const netmind::Graph graph = loadSample("medium10.json");
    netmind::BellmanFordRouting bellmanFord;
    netmind::DijkstraRouting dijkstra;
    netmind::LatencyWeight policy;

    for (const netmind::NodeId src : graph.nodeIds()) {
        for (const netmind::NodeId dst : graph.nodeIds()) {
            const auto bfRoute = bellmanFord.computeRoute(graph, src, dst, policy);
            const auto dijkstraRoute = dijkstra.computeRoute(graph, src, dst, policy);

            REQUIRE(bfRoute.size() == dijkstraRoute.size());
            if (!bfRoute.empty()) {
                REQUIRE(bfRoute.front() == src);
                REQUIRE(bfRoute.back() == dst);
            }
        }
    }
}

TEST_CASE("Bellman-Ford computes next hops for reachable destinations") {
    const netmind::Graph graph = loadSample("small4.json");
    netmind::BellmanFordRouting routing;
    netmind::LatencyWeight policy;

    const auto table = routing.computeTable(graph, 1, policy);

    REQUIRE(table.size() == 3);
    REQUIRE(table.find(1) == table.end());
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.at(3) == 2);
    REQUIRE(table.at(4) == 2);
}

TEST_CASE("Bellman-Ford omits unreachable destinations") {
    netmind::Graph graph = loadSample("small4.json");
    REQUIRE(graph.removeLink(2));

    netmind::BellmanFordRouting routing;
    netmind::LatencyWeight policy;

    REQUIRE(routing.computeRoute(graph, 1, 4, policy).empty());

    const auto table = routing.computeTable(graph, 1, policy);
    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

TEST_CASE("Bellman-Ford ignores down nodes and links") {
    netmind::Graph graph = loadSample("small4.json");
    auto* router = graph.getNode(3);
    REQUIRE(router != nullptr);
    router->setUp(false);

    netmind::BellmanFordRouting routing;
    netmind::LatencyWeight policy;

    const auto table = routing.computeTable(graph, 1, policy);

    REQUIRE(table.size() == 1);
    REQUIRE(table.at(2) == 2);
    REQUIRE(table.find(3) == table.end());
    REQUIRE(table.find(4) == table.end());
}

TEST_CASE("Bellman-Ford breaks equal-cost ties by smaller next-hop id") {
    const netmind::Graph graph = makeEqualCostGraph();
    netmind::BellmanFordRouting routing;
    netmind::LatencyWeight policy;

    REQUIRE(routing.computeRoute(graph, 1, 5, policy) == std::vector<netmind::NodeId>{1, 2, 3, 5});

    const auto table = routing.computeTable(graph, 1, policy);
    REQUIRE(table.at(5) == 2);
}

TEST_CASE("Bellman-Ford detects a reachable negative cycle") {
    netmind::Graph graph;

    graph.addNode(std::make_unique<netmind::Router>(1, "R1", "10.7.0.1"));
    graph.addNode(std::make_unique<netmind::Router>(2, "R2", "10.7.0.2"));
    graph.addLink(std::make_unique<netmind::Link>(1, 1, 2, 100.0, 1.0, 0.0, 16));

    ConstantWeight negativeWeight(-1.0);
    netmind::BellmanFordRouting routing;

    REQUIRE_THROWS_AS(routing.computeRoute(graph, 1, 2, negativeWeight), netmind::ConfigError);
}

TEST_CASE("Bellman-Ford identifies itself by name") {
    netmind::BellmanFordRouting routing;
    REQUIRE(routing.name() == "Bellman-Ford");
}