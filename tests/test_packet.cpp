#include "netmind/model/Packet.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("Packet has the expected default values") {
    const netmind::Packet packet;

    REQUIRE(packet.id == 0);
    REQUIRE(packet.type == netmind::PacketType::DATA);
    REQUIRE(packet.ttl == netmind::Packet::DEFAULT_TTL);
    REQUIRE(packet.status == netmind::PacketStatus::CREATED);
    REQUIRE(packet.pathTaken.empty());
}

TEST_CASE("Packet reply clone swaps endpoints and resets per-transmission state") {
    netmind::Packet request;
    request.id = 10;
    request.srcId = 3;
    request.dstId = 1;
    request.flowId = 7;
    request.sizeBytes = 64;
    request.type = netmind::PacketType::ECHO_REQUEST;
    request.ttl = 12;
    request.timestampSent = 1.5;
    request.originTime = 1.25;
    request.timestampDelivered = 2.0;
    request.status = netmind::PacketStatus::DELIVERED;
    request.pathTaken = {3, 2, 1};
    request.dropReason = netmind::DropReason::RANDOM_LOSS;

    const netmind::Packet reply =
        netmind::Packet::cloneForReply(request, 11);

    REQUIRE(reply.id == 11);
    REQUIRE(reply.srcId == 1);
    REQUIRE(reply.dstId == 3);
    REQUIRE(reply.flowId == 7);
    REQUIRE(reply.sizeBytes == 64);
    REQUIRE(reply.type == netmind::PacketType::ECHO_REPLY);
    REQUIRE(reply.ttl == netmind::Packet::DEFAULT_TTL);
    REQUIRE(reply.timestampSent == 0.0);
    REQUIRE(reply.originTime == 1.25);
    REQUIRE(reply.timestampDelivered == 0.0);
    REQUIRE(reply.status == netmind::PacketStatus::CREATED);
    REQUIRE(reply.pathTaken.empty());
    REQUIRE(reply.dropReason == netmind::DropReason::NO_ROUTE);

    // Cloning leaves the original request unchanged.
    REQUIRE(request.id == 10);
    REQUIRE(request.srcId == 3);
    REQUIRE(request.dstId == 1);
    REQUIRE(request.pathTaken == std::vector<netmind::NodeId>{3, 2, 1});
}

TEST_CASE("Packet retransmission clone keeps packet content and resets transmission state") {
    netmind::Packet original;
    original.id = 20;
    original.srcId = 2;
    original.dstId = 9;
    original.flowId = 4;
    original.sizeBytes = 512;
    original.type = netmind::PacketType::DATA;
    original.ttl = 5;
    original.timestampSent = 3.0;
    original.originTime = 2.0;
    original.timestampDelivered = 4.0;
    original.status = netmind::PacketStatus::DROPPED;
    original.pathTaken = {2, 5, 7};
    original.dropReason = netmind::DropReason::LINK_DOWN;

    const netmind::Packet retransmit =
        netmind::Packet::cloneForRetransmit(original, 21);

    REQUIRE(retransmit.id == 21);
    REQUIRE(retransmit.srcId == 2);
    REQUIRE(retransmit.dstId == 9);
    REQUIRE(retransmit.flowId == 4);
    REQUIRE(retransmit.sizeBytes == 512);
    REQUIRE(retransmit.type == netmind::PacketType::DATA);
    REQUIRE(retransmit.ttl == netmind::Packet::DEFAULT_TTL);
    REQUIRE(retransmit.timestampSent == 0.0);
    REQUIRE(retransmit.originTime == 2.0);
    REQUIRE(retransmit.timestampDelivered == 0.0);
    REQUIRE(retransmit.status == netmind::PacketStatus::CREATED);
    REQUIRE(retransmit.pathTaken.empty());
    REQUIRE(retransmit.dropReason == netmind::DropReason::NO_ROUTE);

    // Cloning leaves the original packet unchanged.
    REQUIRE(original.id == 20);
    REQUIRE(original.pathTaken == std::vector<netmind::NodeId>{2, 5, 7});
}