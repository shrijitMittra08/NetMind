#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "netmind/common/Errors.h"
#include "netmind/graph/TopologyLoader.h"
#include "netmind/model/ForwardingTable.h"
#include "netmind/model/Router.h"
#include "netmind/routing/RoutingManager.h"

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

// Checks that a new routing manager uses Dijkstra, latency weights and zero reconvergence delay
TEST_CASE("RoutingManager defaults to Dijkstra with latency weights") {
    const netmind::RoutingManager manager;

    REQUIRE(manager.algorithmName() == "dijkstra");
    REQUIRE(manager.policyName() == "latency");
    REQUIRE(manager.reconvergenceDelay() == 0.0);
}

// Checks that algorithm and policy can be changed by name and bad names throw without changing them
TEST_CASE("RoutingManager changes algorithms and policies by factory name") {
    netmind::RoutingManager manager;

    manager.setAlgorithm("bfs");
    manager.setPolicy("hops");

    REQUIRE(manager.algorithmName() == "bfs");
    REQUIRE(manager.policyName() == "hops");

    REQUIRE_THROWS_AS(manager.setAlgorithm("unknown"), netmind::ConfigError);
    REQUIRE_THROWS_AS(manager.setPolicy("unknown"), netmind::ConfigError);

    REQUIRE(manager.algorithmName() == "bfs");
    REQUIRE(manager.policyName() == "hops");
}

// Checks that recomputing fills in correct next hops for every router in the small topology
TEST_CASE("RoutingManager recomputes tables for every up router") {
    netmind::Graph graph = loadSample("small4.json");
    netmind::RoutingManager manager;

    const auto summary = manager.recomputeAll(graph);

    REQUIRE(summary.routersProcessed == 2);
    REQUIRE(summary.entriesChanged > 0);

    const auto* router1 = dynamic_cast<const netmind::Router*>(graph.getNode(2));
    const auto* router2 = dynamic_cast<const netmind::Router*>(graph.getNode(3));

    REQUIRE(router1 != nullptr);
    REQUIRE(router2 != nullptr);
    REQUIRE(router1->forwardingTable().nextHop(4) == 3);
    REQUIRE(router2->forwardingTable().nextHop(1) == 2);
}

// Checks that recomputing a second time with no changes reports zero changed entries
TEST_CASE("RoutingManager reports zero changes for a stable recomputation") {
    netmind::Graph graph = loadSample("small4.json");
    netmind::RoutingManager manager;

    (void)manager.recomputeAll(graph);
    const auto secondSummary = manager.recomputeAll(graph);

    REQUIRE(secondSummary.routersProcessed == 2);
    REQUIRE(secondSummary.entriesChanged == 0);
}

// Checks that a router switches to the backup path after a link is removed
TEST_CASE("RoutingManager updates routes after a link is removed") {
    netmind::Graph graph = loadSample("ring_backup.json");
    netmind::RoutingManager manager;

    (void)manager.recomputeAll(graph);

    auto* router1 = dynamic_cast<netmind::Router*>(graph.getNode(2));
    REQUIRE(router1 != nullptr);
    REQUIRE(router1->forwardingTable().nextHop(6) == 3);

    REQUIRE(graph.removeLink(2));
    const auto summary = manager.recomputeAll(graph);

    REQUIRE(summary.entriesChanged > 0);
    REQUIRE(router1->forwardingTable().nextHop(6) == 5);
}

// Checks that a static route is kept and not overwritten when routes are recomputed
TEST_CASE("RoutingManager preserves static entries during recomputation") {
    netmind::Graph graph = loadSample("ring_backup.json");
    netmind::RoutingManager manager;

    manager.addStaticRoute(graph, 2, 6, 5);
    (void)manager.recomputeAll(graph);

    const auto* router = dynamic_cast<const netmind::Router*>(graph.getNode(2));
    REQUIRE(router != nullptr);

    const auto entry = router->forwardingTable().lookup(6);
    REQUIRE(entry.has_value());
    REQUIRE(entry->nextHop == 5);
    REQUIRE(entry->source == netmind::ForwardingTable::Source::STATIC);
}

// Checks that routes can be recomputed for just one chosen router
TEST_CASE("RoutingManager recomputes one router at a time") {
    netmind::Graph graph = loadSample("small4.json");
    netmind::RoutingManager manager;

    const auto summary = manager.recomputeFor(graph, 2);

    REQUIRE(summary.routersProcessed == 1);
    REQUIRE(summary.entriesChanged > 0);

    const auto* router = dynamic_cast<const netmind::Router*>(graph.getNode(2));
    REQUIRE(router != nullptr);
    REQUIRE(router->forwardingTable().nextHop(4) == 3);
}

// Checks that recomputing for a host or a missing node throws a topology error
TEST_CASE("RoutingManager rejects recomputation for a non-router") {
    netmind::Graph graph = loadSample("small4.json");
    netmind::RoutingManager manager;

    REQUIRE_THROWS_AS(manager.recomputeFor(graph, 1), netmind::TopologyError);
    REQUIRE_THROWS_AS(manager.recomputeFor(graph, 99), netmind::TopologyError);
}

// Checks that routers that are down are skipped when all routes are recomputed
TEST_CASE("RoutingManager skips down routers during full recomputation") {
    netmind::Graph graph = loadSample("small4.json");
    auto* router = graph.getNode(3);
    REQUIRE(router != nullptr);
    router->setUp(false);

    netmind::RoutingManager manager;
    const auto summary = manager.recomputeAll(graph);

    REQUIRE(summary.routersProcessed == 1);
}

// Checks that a route explanation lists the path, each hop cost and the total cost
TEST_CASE("RoutingManager explains routes and their hop costs") {
    const netmind::Graph graph = loadSample("diamond_weighted.json");
    netmind::RoutingManager manager;

    const auto explanation = manager.explainRouteDetailed(graph, 1, 8);

    REQUIRE(explanation.path == std::vector<netmind::NodeId>{1, 2, 3, 4, 5, 7, 8});
    REQUIRE(explanation.hopCosts == std::vector<double>{1.0, 2.0, 2.0, 2.0, 2.0, 1.0});
    REQUIRE(explanation.totalCost == 10.0);
    REQUIRE(manager.explainRoute(graph, 1, 8) == explanation.path);
}

// Checks that explaining an unreachable route gives an empty path and zero cost
TEST_CASE("RoutingManager reports no explanation for unreachable routes") {
    netmind::Graph graph = loadSample("small4.json");
    REQUIRE(graph.removeLink(2));

    netmind::RoutingManager manager;
    const auto explanation = manager.explainRouteDetailed(graph, 1, 4);

    REQUIRE(explanation.path.empty());
    REQUIRE(explanation.hopCosts.empty());
    REQUIRE(explanation.totalCost == 0.0);
}

// Checks that a valid reconvergence delay is saved and a negative one throws an error
TEST_CASE("RoutingManager validates reconvergence delay") {
    netmind::RoutingManager manager;

    manager.setReconvergenceDelay(0.25);
    REQUIRE(manager.reconvergenceDelay() == 0.25);

    REQUIRE_THROWS_AS(manager.setReconvergenceDelay(-1.0), netmind::ConfigError);
}