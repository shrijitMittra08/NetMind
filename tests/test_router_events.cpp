#include "netmind/SimObserver.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/simulation/SimulationEngine.h"
#include "netmind/simulation/events/RouterFailureEvent.h"
#include "netmind/simulation/events/RouterRestoreEvent.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

class RouterEventRecorder final : public netmind::SimObserver {
public:
    void onDropped(
        netmind::SimTime,
        const netmind::Packet&,
        const std::string&,
        netmind::DropReason reason
    ) override {
        dropReasons.push_back(reason);
    }

    void onNetworkEvent(
        netmind::SimTime,
        const std::string& type,
        const std::string&,
        const std::string&
    ) override {
        eventTypes.push_back(type);
    }

    std::vector<netmind::DropReason> dropReasons;
    std::vector<std::string> eventTypes;
};

std::unique_ptr<netmind::Graph> makeRouterGraph() {
    auto graph = std::make_unique<netmind::Graph>();

    graph->addNode(std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1"));
    graph->addNode(std::make_unique<netmind::Router>(
        2, "Router", "10.0.0.2"));
    graph->addNode(std::make_unique<netmind::Host>(
        3, "HostB", "10.0.0.3"));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 10.0, 2.0, 0.0, 4));
    graph->addLink(std::make_unique<netmind::Link>(
        11, 2, 3, 10.0, 2.0, 0.0, 4));

    return graph;
}

netmind::Packet makePacket(netmind::PacketId id) {
    netmind::Packet packet;
    packet.id = id;
    packet.srcId = 1;
    packet.dstId = 3;
    packet.sizeBytes = 1000;
    return packet;
}

}             

TEST_CASE("Router failure drops packets queued on adjacent links") {
    netmind::SimulationEngine engine(1234);
    RouterEventRecorder observer;
    engine.addObserver(observer);
    engine.setGraph(makeRouterGraph());

                                                                         
    engine.transmit(makePacket(1), 1, 2);
    engine.transmit(makePacket(2), 1, 2);
    engine.scheduleEvent(
        0.0001,
        std::make_shared<netmind::RouterFailureEvent>(2));

    engine.run();

    REQUIRE(observer.dropReasons.size() == 2);
    REQUIRE(observer.dropReasons[0] == netmind::DropReason::NODE_DOWN);
    REQUIRE(observer.dropReasons[1] == netmind::DropReason::NODE_DOWN);
    REQUIRE(observer.eventTypes.size() == 1);
    REQUIRE(observer.eventTypes[0] == "ROUTER_DOWN");

    const netmind::Node* router = engine.graph().getNode(2);
    REQUIRE(router != nullptr);
    REQUIRE_FALSE(router->isUp());
}

TEST_CASE("Router failure invalidates a packet already in flight") {
    netmind::SimulationEngine engine(1234);
    RouterEventRecorder observer;
    engine.addObserver(observer);
    engine.setGraph(makeRouterGraph());

                                                                         
                                                             
    engine.transmit(makePacket(1), 1, 2);
    engine.scheduleEvent(
        0.001,
        std::make_shared<netmind::RouterFailureEvent>(2));

    engine.run();

    REQUIRE(observer.dropReasons.size() == 1);
    REQUIRE(observer.dropReasons[0] == netmind::DropReason::NODE_DOWN);
}

TEST_CASE("Router recovery restores the router and emits both events") {
    netmind::SimulationEngine engine(1234);
    RouterEventRecorder observer;
    engine.addObserver(observer);
    engine.setGraph(makeRouterGraph());

    engine.scheduleEvent(
        1.0,
        std::make_shared<netmind::RouterFailureEvent>(2));
    engine.scheduleEvent(
        2.0,
        std::make_shared<netmind::RouterRestoreEvent>(2));

    engine.run();

    const netmind::Node* router = engine.graph().getNode(2);
    REQUIRE(router != nullptr);
    REQUIRE(router->isUp());

    REQUIRE(observer.eventTypes.size() == 2);
    REQUIRE(observer.eventTypes[0] == "ROUTER_DOWN");
    REQUIRE(observer.eventTypes[1] == "ROUTER_UP");
}