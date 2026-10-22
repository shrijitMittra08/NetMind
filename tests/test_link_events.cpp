#include "netmind/SimObserver.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/simulation/SimulationEngine.h"
#include "netmind/simulation/events/LinkFailureEvent.h"
#include "netmind/simulation/events/LinkRestoreEvent"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

class EventRecorder final : public netmind::SimObserver {
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

std::unique_ptr<netmind::Graph> makeTwoHostGraph() {
    auto graph = std::make_unique<netmind::Graph>();

    graph->addNode(std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1"));
    graph->addNode(std::make_unique<netmind::Host>(
        2, "HostB", "10.0.0.2"));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 10.0, 2.0, 0.0, 4));

    return graph;
}

netmind::Packet makePacket(netmind::PacketId id) {
    netmind::Packet packet;
    packet.id = id;
    packet.srcId = 1;
    packet.dstId = 2;
    packet.sizeBytes = 1000;
    return packet;
}

}             

TEST_CASE("Link failure flushes queued packets in both directions") {
    netmind::SimulationEngine engine(1234);
    EventRecorder observer;
    engine.addObserver(observer);
    engine.setGraph(makeTwoHostGraph());

                                                                    
                                                                  
    engine.transmit(makePacket(1), 1, 2);
    engine.transmit(makePacket(2), 1, 2);
    engine.scheduleEvent(
        0.0001,
        std::make_shared<netmind::LinkFailureEvent>(10));

    engine.run();

    REQUIRE(observer.dropReasons.size() == 2);
    REQUIRE(observer.dropReasons[0] == netmind::DropReason::LINK_DOWN);
    REQUIRE(observer.dropReasons[1] == netmind::DropReason::LINK_DOWN);
    REQUIRE(observer.eventTypes.size() == 1);
    REQUIRE(observer.eventTypes[0] == "LINK_DOWN");

    const netmind::Link* link = engine.graph().getLink(10);
    REQUIRE(link != nullptr);
    REQUIRE(link->state() == netmind::LinkState::DOWN);
}

TEST_CASE("Link failure invalidates a packet already in flight") {
    netmind::SimulationEngine engine(1234);
    EventRecorder observer;
    engine.addObserver(observer);
    engine.setGraph(makeTwoHostGraph());

    engine.transmit(makePacket(1), 1, 2);

                                                                     
                                                              
    engine.scheduleEvent(
        0.001,
        std::make_shared<netmind::LinkFailureEvent>(10));

    engine.run();

    REQUIRE(observer.dropReasons.size() == 1);
    REQUIRE(observer.dropReasons[0] == netmind::DropReason::LINK_DOWN);

    const netmind::Link* link = engine.graph().getLink(10);
    REQUIRE(link != nullptr);
    REQUIRE(link->state() == netmind::LinkState::DOWN);
}

TEST_CASE("Link restore returns a failed link to the up state") {
    netmind::SimulationEngine engine(1234);
    EventRecorder observer;
    engine.addObserver(observer);
    engine.setGraph(makeTwoHostGraph());

    engine.scheduleEvent(
        1.0,
        std::make_shared<netmind::LinkFailureEvent>(10));
    engine.scheduleEvent(
        2.0,
        std::make_shared<netmind::LinkRestoreEvent>(10));

    engine.run();

    const netmind::Link* link = engine.graph().getLink(10);
    REQUIRE(link != nullptr);
    REQUIRE(link->state() == netmind::LinkState::UP);

    REQUIRE(observer.eventTypes.size() == 2);
    REQUIRE(observer.eventTypes[0] == "LINK_DOWN");
    REQUIRE(observer.eventTypes[1] == "LINK_UP");
}