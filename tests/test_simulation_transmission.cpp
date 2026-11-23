#include "netmind/SimObserver.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/simulation/SimulationEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

namespace {

class DeliveryRecorder final : public netmind::SimObserver {
public:
    void onSent(netmind::SimTime, const netmind::Packet&) override {
        ++sentCount;
    }

    void onEnqueue(
        netmind::SimTime,
        const netmind::Packet&,
        const netmind::ChannelId&,
        std::size_t
    ) override {
        ++enqueueCount;
    }

    void onTxStart(
        netmind::SimTime,
        const netmind::Packet&,
        const netmind::ChannelId&,
        netmind::SimTime
    ) override {
        ++transmissionCount;
    }

    void onForward(
        netmind::SimTime,
        const netmind::Packet&,
        netmind::NodeId,
        netmind::NodeId
    ) override {
        ++forwardCount;
    }

    void onDelivered(
        netmind::SimTime time,
        const netmind::Packet& packet
    ) override {
        ++deliveryCount;
        deliveredAt = time;
        deliveredPacketId = packet.id;
    }

    int sentCount{};
    int enqueueCount{};
    int transmissionCount{};
    int forwardCount{};
    int deliveryCount{};
    netmind::SimTime deliveredAt{};
    netmind::PacketId deliveredPacketId{};
};

}             

// Checks that a packet goes from host to host through a router and arrives at the expected time
TEST_CASE("SimulationEngine transmits a packet through a router") {
    auto graph = std::make_unique<netmind::Graph>();

    auto hostA = std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1");
    netmind::Host* hostAPointer = hostA.get();
    hostA->setGatewayId(2);

    auto router = std::make_unique<netmind::Router>(
        2, "Router1", "10.0.0.2");
    netmind::Router* routerPointer = router.get();
    routerPointer->forwardingTable().install(3, 3, 1.0);

    auto hostB = std::make_unique<netmind::Host>(
        3, "HostB", "10.0.0.3");

    graph->addNode(std::move(hostA));
    graph->addNode(std::move(router));
    graph->addNode(std::move(hostB));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 10.0, 2.0, 0.0, 8));
    graph->addLink(std::make_unique<netmind::Link>(
        11, 2, 3, 10.0, 2.0, 0.0, 8));

    netmind::SimulationEngine engine(1234);
    DeliveryRecorder observer;
    engine.addObserver(observer);
    engine.setGraph(std::move(graph));

    netmind::Packet packet;
    packet.id = engine.allocatePacketId();
    packet.srcId = 1;
    packet.dstId = 3;
    packet.sizeBytes = 1000;

    hostAPointer->originate(packet, engine);
    engine.run();

    REQUIRE(observer.sentCount == 1);
    REQUIRE(observer.enqueueCount == 2);
    REQUIRE(observer.transmissionCount == 2);
    REQUIRE(observer.forwardCount == 1);
    REQUIRE(observer.deliveryCount == 1);
    REQUIRE(observer.deliveredPacketId == packet.id);

                                                                     
    REQUIRE(std::abs(observer.deliveredAt - 0.0056) < 1e-12);
    REQUIRE(engine.finished());
}