#include "netmind/model/Host.h"
#include "netmind/simulation/SimContext.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace {

class MockSimContext final : public netmind::SimContext {
public:
    netmind::SimTime currentTime{2.0};
    netmind::PacketId nextId{100};
    netmind::Rng random{42};
    netmind::Graph topology;

    std::optional<netmind::Packet> transmitted;
    std::optional<netmind::Packet> delivered;
    std::optional<netmind::Packet> dropped;

    netmind::NodeId transmitFrom{};
    netmind::NodeId transmitTo{};
    netmind::NodeId deliveredAt{};
    netmind::NodeId droppedAt{};
    netmind::DropReason dropReason{netmind::DropReason::NO_ROUTE};

    netmind::SimTime now() const override {
        return currentTime;
    }

    netmind::PacketId allocatePacketId() override {
        return nextId++;
    }

    void transmit(
        netmind::Packet pkt,
        netmind::NodeId from,
        netmind::NodeId to) override {
        transmitted = std::move(pkt);
        transmitFrom = from;
        transmitTo = to;
    }

    void deliver(
        const netmind::Packet& pkt,
        netmind::NodeId at) override {
        delivered = pkt;
        deliveredAt = at;
    }

    void drop(
        const netmind::Packet& pkt,
        netmind::NodeId at,
        netmind::DropReason why) override {
        dropped = pkt;
        droppedAt = at;
        dropReason = why;
    }

    netmind::Rng& rng() override {
        return random;
    }

    const netmind::Graph& graph() const override {
        return topology;
    }
};

} // namespace

TEST_CASE("Host delivers a packet addressed to itself") {
    netmind::Host host(1, "PC1", "10.0.0.1");
    MockSimContext ctx;

    netmind::Packet pkt;
    pkt.id = 20;
    pkt.srcId = 2;
    pkt.dstId = 1;
    pkt.type = netmind::PacketType::DATA;

    host.receivePacket(pkt, 4, ctx);

    REQUIRE(ctx.delivered.has_value());
    REQUIRE(ctx.delivered->id == 20);
    REQUIRE(ctx.deliveredAt == 1);
    REQUIRE_FALSE(ctx.transmitted.has_value());
    REQUIRE_FALSE(ctx.dropped.has_value());
}

TEST_CASE("Host drops a packet addressed to another node") {
    netmind::Host host(1, "PC1", "10.0.0.1");
    MockSimContext ctx;

    netmind::Packet pkt;
    pkt.id = 21;
    pkt.srcId = 2;
    pkt.dstId = 3;

    host.receivePacket(pkt, 4, ctx);

    REQUIRE(ctx.dropped.has_value());
    REQUIRE(ctx.dropped->id == 21);
    REQUIRE(ctx.droppedAt == 1);
    REQUIRE(ctx.dropReason == netmind::DropReason::NO_ROUTE);
    REQUIRE_FALSE(ctx.delivered.has_value());
}

TEST_CASE("Host replies to an echo request through its gateway") {
    netmind::Host host(1, "PC1", "10.0.0.1");
    host.setGatewayId(2);

    MockSimContext ctx;
    ctx.currentTime = 2.0;

    netmind::Packet request;
    request.id = 30;
    request.srcId = 3;
    request.dstId = 1;
    request.flowId = 7;
    request.sizeBytes = 64;
    request.type = netmind::PacketType::ECHO_REQUEST;
    request.originTime = 0.5;

    host.receivePacket(request, 4, ctx);

    REQUIRE(ctx.transmitted.has_value());
    REQUIRE(ctx.transmitted->id == 100);
    REQUIRE(ctx.transmitted->srcId == 1);
    REQUIRE(ctx.transmitted->dstId == 3);
    REQUIRE(ctx.transmitted->flowId == 7);
    REQUIRE(ctx.transmitted->sizeBytes == 64);
    REQUIRE(ctx.transmitted->type == netmind::PacketType::ECHO_REPLY);
    REQUIRE(ctx.transmitted->originTime == 0.5);
    REQUIRE(ctx.transmitted->timestampSent == 2.0);
    REQUIRE(ctx.transmitted->pathTaken == std::vector<netmind::NodeId>{1});
    REQUIRE(ctx.transmitFrom == 1);
    REQUIRE(ctx.transmitTo == 2);
    REQUIRE_FALSE(ctx.delivered.has_value());
    REQUIRE_FALSE(ctx.dropped.has_value());
}

TEST_CASE("Host drops an originated packet when no gateway is configured") {
    netmind::Host host(1, "PC1", "10.0.0.1");
    MockSimContext ctx;

    netmind::Packet pkt;
    pkt.id = 40;
    pkt.srcId = 1;
    pkt.dstId = 3;

    host.originate(pkt, ctx);

    REQUIRE(ctx.dropped.has_value());
    REQUIRE(ctx.dropped->id == 40);
    REQUIRE(ctx.dropReason == netmind::DropReason::NO_ROUTE);
    REQUIRE_FALSE(ctx.transmitted.has_value());
}