#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Packet.h"
#include "netmind/model/Switch.h"
#include "netmind/simulation/SimContext.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace {

class SwitchTestContext final : public netmind::SimContext {
public:
    struct Transmission {
        netmind::Packet packet;
        netmind::NodeId from;
        netmind::NodeId to;
    };

    netmind::Graph topology;
    std::vector<Transmission> transmissions;
    netmind::Rng random{123};

    netmind::SimTime now() const override {
        return 0.0;
    }

    netmind::PacketId allocatePacketId() override {
        return 1;
    }

    void transmit(
        netmind::Packet pkt,
        netmind::NodeId from,
        netmind::NodeId to
    ) override {
        transmissions.push_back(
            Transmission{std::move(pkt), from, to});
    }

    void deliver(
        const netmind::Packet&,
        netmind::NodeId
    ) override {}

    void drop(
        const netmind::Packet&,
        netmind::NodeId,
        netmind::DropReason
    ) override {}

    netmind::Rng& rng() override {
        return random;
    }

    const netmind::Graph& graph() const override {
        return topology;
    }
};

void addSwitchTestTopology(SwitchTestContext& ctx) {
    ctx.topology.addNode(
        std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));
    ctx.topology.addNode(
        std::make_unique<netmind::Switch>(2, "SwitchA", "10.0.0.2"));
    ctx.topology.addNode(
        std::make_unique<netmind::Host>(3, "HostB", "10.0.0.3"));
    ctx.topology.addNode(
        std::make_unique<netmind::Host>(4, "HostC", "10.0.0.4"));

    ctx.topology.addLink(
        std::make_unique<netmind::Link>(10, 1, 2, 100.0, 1.0, 0.0, 16));
    ctx.topology.addLink(
        std::make_unique<netmind::Link>(11, 2, 3, 100.0, 1.0, 0.0, 16));
    ctx.topology.addLink(
        std::make_unique<netmind::Link>(12, 2, 4, 100.0, 1.0, 0.0, 16));
}

} // namespace

TEST_CASE("Switch floods to active neighbors except the incoming link") {
    SwitchTestContext ctx;
    addSwitchTestTopology(ctx);

    auto* networkSwitch =
        dynamic_cast<netmind::Switch*>(ctx.topology.getNode(2));
    REQUIRE(networkSwitch != nullptr);

    netmind::Packet packet;
    packet.id = 50;
    packet.srcId = 1;
    packet.dstId = 3;

    networkSwitch->receivePacket(packet, 10, ctx);

    REQUIRE(ctx.transmissions.size() == 2);
    REQUIRE(ctx.transmissions[0].from == 2);
    REQUIRE(ctx.transmissions[1].from == 2);
    REQUIRE(
        (ctx.transmissions[0].to == 3 || ctx.transmissions[0].to == 4));
    REQUIRE(
        (ctx.transmissions[1].to == 3 || ctx.transmissions[1].to == 4));
    REQUIRE(ctx.transmissions[0].to != ctx.transmissions[1].to);
    REQUIRE(ctx.transmissions[0].packet.id == 50);
    REQUIRE(ctx.transmissions[1].packet.id == 50);
}

TEST_CASE("Switch does not forward the same packet id twice") {
    SwitchTestContext ctx;
    addSwitchTestTopology(ctx);

    auto* networkSwitch =
        dynamic_cast<netmind::Switch*>(ctx.topology.getNode(2));
    REQUIRE(networkSwitch != nullptr);

    netmind::Packet packet;
    packet.id = 51;

    networkSwitch->receivePacket(packet, 10, ctx);
    REQUIRE(ctx.transmissions.size() == 2);

    networkSwitch->receivePacket(packet, 10, ctx);
    REQUIRE(ctx.transmissions.size() == 2);
}

TEST_CASE("Switch floods only over active links") {
    SwitchTestContext ctx;
    addSwitchTestTopology(ctx);

    auto* networkSwitch =
        dynamic_cast<netmind::Switch*>(ctx.topology.getNode(2));
    REQUIRE(networkSwitch != nullptr);

    auto* hostC = ctx.topology.getNode(4);
    REQUIRE(hostC != nullptr);
    hostC->setUp(false);

    netmind::Packet packet;
    packet.id = 52;

    networkSwitch->receivePacket(packet, 10, ctx);

    REQUIRE(ctx.transmissions.size() == 1);
    REQUIRE(ctx.transmissions.front().to == 3);
}