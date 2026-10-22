#include "netmind/model/Router.h"
#include "netmind/simulation/SimContext.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace {

class MockSimContext final : public netmind::SimContext {
public:
    netmind::SimTime currentTime{1.0};
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

}             

TEST_CASE("Forwarding table keeps static routes when dynamic routes are installed") {
    netmind::ForwardingTable table;

    table.addStatic(9, 4, 2.5);
    table.install(9, 5, 1.0);

    const auto route = table.lookup(9);

    REQUIRE(route.has_value());
    REQUIRE(route->nextHop == 4);
    REQUIRE(route->cost == 2.5);
    REQUIRE(route->source == netmind::ForwardingTable::Source::STATIC);
}

TEST_CASE("Forwarding table clears dynamic routes and keeps static routes") {
    netmind::ForwardingTable table;

    table.install(8, 3, 1.0);
    table.addStatic(9, 4, 2.5);

    table.clearDynamic();

    REQUIRE_FALSE(table.contains(8));
    REQUIRE(table.contains(9));
}

TEST_CASE("Forwarding table returns routes sorted by destination") {
    netmind::ForwardingTable table;

    table.install(9, 4, 1.0);
    table.install(3, 2, 1.0);

    const auto routes = table.toSortedVector();

    REQUIRE(routes.size() == 2);
    REQUIRE(routes[0].first == 3);
    REQUIRE(routes[1].first == 9);
}

TEST_CASE("Forwarding table preserves the existing next-hop interface") {
    netmind::ForwardingTable table;

    table.setNextHop(9, 4);

    REQUIRE(table.contains(9));
    REQUIRE(table.nextHop(9).has_value());
    REQUIRE(*table.nextHop(9) == 4);
}

TEST_CASE("Router forwards using its route and decreases TTL") {
    netmind::Router router(2, "R1", "10.0.0.2");
    router.forwardingTable().install(9, 4, 1.0);

    MockSimContext ctx;
    netmind::Packet pkt;
    pkt.id = 50;
    pkt.srcId = 1;
    pkt.dstId = 9;
    pkt.ttl = 5;

    router.receivePacket(pkt, 7, ctx);

    REQUIRE(ctx.transmitted.has_value());
    REQUIRE(ctx.transmitted->ttl == 4);
    REQUIRE(ctx.transmitted->pathTaken == std::vector<netmind::NodeId>{2});
    REQUIRE(ctx.transmitFrom == 2);
    REQUIRE(ctx.transmitTo == 4);
    REQUIRE_FALSE(ctx.delivered.has_value());
    REQUIRE_FALSE(ctx.dropped.has_value());
}

TEST_CASE("Router drops a packet whose TTL is about to expire") {
    netmind::Router router(2, "R1", "10.0.0.2");
    MockSimContext ctx;

    netmind::Packet pkt;
    pkt.id = 51;
    pkt.dstId = 9;
    pkt.ttl = 1;

    router.receivePacket(pkt, 7, ctx);

    REQUIRE(ctx.dropped.has_value());
    REQUIRE(ctx.dropped->id == 51);
    REQUIRE(ctx.droppedAt == 2);
    REQUIRE(ctx.dropReason == netmind::DropReason::TTL_EXPIRED);
    REQUIRE_FALSE(ctx.transmitted.has_value());
}

TEST_CASE("Router drops a packet when no route exists") {
    netmind::Router router(2, "R1", "10.0.0.2");
    MockSimContext ctx;

    netmind::Packet pkt;
    pkt.id = 52;
    pkt.dstId = 9;
    pkt.ttl = 5;

    router.receivePacket(pkt, 7, ctx);

    REQUIRE(ctx.dropped.has_value());
    REQUIRE(ctx.dropped->id == 52);
    REQUIRE(ctx.dropReason == netmind::DropReason::NO_ROUTE);
    REQUIRE_FALSE(ctx.transmitted.has_value());
}

TEST_CASE("Router delivers a packet addressed to itself") {
    netmind::Router router(2, "R1", "10.0.0.2");
    MockSimContext ctx;

    netmind::Packet pkt;
    pkt.id = 53;
    pkt.dstId = 2;
    pkt.ttl = 5;

    router.receivePacket(pkt, 7, ctx);

    REQUIRE(ctx.delivered.has_value());
    REQUIRE(ctx.delivered->id == 53);
    REQUIRE(ctx.delivered->ttl == 4);
    REQUIRE(ctx.deliveredAt == 2);
    REQUIRE_FALSE(ctx.transmitted.has_value());
    REQUIRE_FALSE(ctx.dropped.has_value());
}