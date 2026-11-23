#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/simulation/SimulationEngine.h"
#include "netmind/simulation/events/LinkFailureEvent.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace {

std::unique_ptr<netmind::Graph> makeBackupPathGraph() {
    auto graph = std::make_unique<netmind::Graph>();

    graph->addNode(std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1"));
    graph->addNode(std::make_unique<netmind::Router>(
        2, "RouterA", "10.0.0.2"));
    graph->addNode(std::make_unique<netmind::Router>(
        3, "RouterB", "10.0.0.3"));
    graph->addNode(std::make_unique<netmind::Router>(
        4, "RouterC", "10.0.0.4"));
    graph->addNode(std::make_unique<netmind::Host>(
        5, "HostB", "10.0.0.5"));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 100.0, 1.0, 0.0, 8));
    graph->addLink(std::make_unique<netmind::Link>(
        11, 2, 3, 100.0, 1.0, 0.0, 8));
    graph->addLink(std::make_unique<netmind::Link>(
        12, 3, 5, 100.0, 1.0, 0.0, 8));
    graph->addLink(std::make_unique<netmind::Link>(
        13, 2, 4, 100.0, 1.0, 0.0, 8));
    graph->addLink(std::make_unique<netmind::Link>(
        14, 4, 3, 100.0, 1.0, 0.0, 8));

    return graph;
}

const netmind::Router& routerA(const netmind::SimulationEngine& engine) {
    const auto* router =
        dynamic_cast<const netmind::Router*>(
            engine.graph().getNode(2));

    REQUIRE(router != nullptr);
    return *router;
}

}             

// Checks that a link failure makes the router switch to the backup path right away
TEST_CASE("Link failure recomputes routes immediately by default") {
    netmind::SimulationEngine engine(1234);
    engine.setGraph(makeBackupPathGraph());

    const auto initialRoute =
        routerA(engine).forwardingTable().lookup(5);
    REQUIRE(initialRoute.has_value());
    REQUIRE(initialRoute->nextHop == 3);

    engine.scheduleEvent(
        1.0,
        std::make_shared<netmind::LinkFailureEvent>(11));
    engine.run();

    const auto rerouted =
        routerA(engine).forwardingTable().lookup(5);
    REQUIRE(rerouted.has_value());
    REQUIRE(rerouted->nextHop == 4);
}

// Checks that routes keep the old next hop until the reconvergence delay has passed
TEST_CASE("Route recomputation waits for configured reconvergence delay") {
    netmind::SimulationEngine engine(1234);
    engine.routingManager().setReconvergenceDelay(0.5);
    engine.setGraph(makeBackupPathGraph());

    const auto initialRoute =
        routerA(engine).forwardingTable().lookup(5);
    REQUIRE(initialRoute.has_value());
    REQUIRE(initialRoute->nextHop == 3);

    engine.scheduleEvent(
        1.0,
        std::make_shared<netmind::LinkFailureEvent>(11));

    engine.runUntil(1.25);

    const auto staleRoute =
        routerA(engine).forwardingTable().lookup(5);
    REQUIRE(staleRoute.has_value());
    REQUIRE(staleRoute->nextHop == 3);

    engine.runUntil(1.5);

    const auto rerouted =
        routerA(engine).forwardingTable().lookup(5);
    REQUIRE(rerouted.has_value());
    REQUIRE(rerouted->nextHop == 4);
}