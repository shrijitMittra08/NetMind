#include "netmind/metrics/MetricsCollector.h"

#include "netmind/model/Host.h"
#include "netmind/model/Link.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

bool near(double actual, double expected, double tolerance = 1e-9) {
    return std::abs(actual - expected) <= tolerance;
}

netmind::Packet makePacket(
    netmind::PacketId id,
    netmind::FlowId flowId,
    netmind::SimTime sentTime,
    netmind::PacketType type = netmind::PacketType::DATA) {
    netmind::Packet packet;
    packet.id = id;
    packet.flowId = flowId;
    packet.srcId = 1;
    packet.dstId = 2;
    packet.sizeBytes = 1000;
    packet.type = type;
    packet.timestampSent = sentTime;
    packet.pathTaken = {1, 3, 2};
    return packet;
}

netmind::Graph makeTwoHostGraph() {
    netmind::Graph graph;
    graph.addNode(std::make_unique<netmind::Host>(1, "PC1", "10.0.0.1"));
    graph.addNode(std::make_unique<netmind::Host>(2, "PC2", "10.0.0.2"));
    graph.addLink(std::make_unique<netmind::Link>(
        7, 1, 2, 10.0, 2.0, 0.0, 8));
    return graph;
}

} // namespace

TEST_CASE("MetricsCollector calculates flow metrics and excludes unfinished "
          "packets from loss") {
    netmind::Graph graph;
    netmind::MetricsCollector metrics(graph);

    metrics.onNetworkEvent(
        0.0, "FLOW_START", "42", "src=1;dst=2");

    const double latenciesMs[] = {10.0, 12.0, 10.0, 14.0};

    for (std::size_t i = 0; i < 4; ++i) {
        const double sentTime = 0.1 * static_cast<double>(i);
        auto packet = makePacket(
            static_cast<netmind::PacketId>(i + 1),
            42,
            sentTime);

        metrics.onSent(sentTime, packet);
        metrics.onDelivered(
            sentTime + latenciesMs[i] / 1000.0,
            packet);
    }

    auto dropped = makePacket(5, 42, 0.5);
    metrics.onSent(0.5, dropped);
    metrics.onDropped(
        0.6,
        dropped,
        "PC1",
        netmind::DropReason::NO_ROUTE);

    auto unfinished = makePacket(6, 42, 0.7);
    metrics.onSent(0.7, unfinished);

    metrics.onNetworkEvent(
        2.0, "FLOW_STOP", "42", "state=stopped");
    metrics.finalize(2.0);

    REQUIRE(metrics.flows().size() == 1);
    const auto& flow = metrics.flows().front();

    CHECK(flow.flowId == 42);
    CHECK(flow.srcId == 1);
    CHECK(flow.dstId == 2);
    CHECK(flow.sent == 6);
    CHECK(flow.delivered == 4);
    CHECK(flow.dropped == 1);
    CHECK(flow.unfinished == 1);
    CHECK(near(flow.lossPct, 20.0));

    CHECK(near(flow.avgLatencyMs, 11.5));
    CHECK(near(flow.minLatencyMs, 10.0));
    CHECK(near(flow.maxLatencyMs, 14.0));
    CHECK(near(flow.p95LatencyMs, 14.0));
    CHECK(near(flow.jitterMs, 8.0 / 3.0));
    CHECK(near(flow.avgHops, 2.0));
    CHECK(near(flow.throughputMbps, 0.016));
    CHECK(near(flow.goodputMbps, 0.016));

    CHECK(near(metrics.network().reliabilityPct, 80.0));
    CHECK(metrics.network().totalDrops == 1);
    CHECK(near(metrics.network().avgLatencyMs, 11.5));
}

TEST_CASE("MetricsCollector calculates time-weighted queue and channel values") {
    auto graph = makeTwoHostGraph();
    netmind::MetricsCollector metrics(graph);

    const netmind::ChannelId channel{
        7,
        netmind::Direction::AtoB};

    auto first = makePacket(1, 1, 0.0);
    auto second = makePacket(2, 1, 0.5);

    metrics.onEnqueue(0.0, first, channel, 1);
    metrics.onEnqueue(0.5, second, channel, 2);

    metrics.onTxStart(0.5, first, channel, 0.5008);
    metrics.onQueueChanged(1.0, channel, 1);

    metrics.onTxStart(1.0, second, channel, 1.0008);
    metrics.onQueueChanged(1.5, channel, 0);

    metrics.onNetworkEvent(
        0.75, "LINK_DOWN", "7", "state=down");
    metrics.onNetworkEvent(
        1.25, "LINK_UP", "7", "state=up");

    metrics.finalize(2.0);

    REQUIRE(metrics.channels().size() == 2);

    const auto result = std::find_if(
        metrics.channels().begin(),
        metrics.channels().end(),
        [](const netmind::ChannelMetrics& item) {
            return item.channel.linkId == 7 &&
                item.channel.dir == netmind::Direction::AtoB;
        });

    REQUIRE(result != metrics.channels().end());

    CHECK(result->name == "PC1>PC2");
    CHECK(result->maxQueue == 2);
    CHECK(near(result->avgQueue, 1.0));
    CHECK(result->txPackets == 2);
    CHECK(near(result->utilizationPct, 0.08));
    CHECK(near(result->downtimeS, 0.5));
}

TEST_CASE("MetricsCollector calculates RTT from an echo reply origin time") {
    netmind::Graph graph;
    netmind::MetricsCollector metrics(graph);

    auto reply = makePacket(
        20,
        8,
        0.25,
        netmind::PacketType::ECHO_REPLY);
    reply.originTime = 0.10;

    metrics.onSent(0.25, reply);
    metrics.onDelivered(0.35, reply);
    metrics.finalize(1.0);

    REQUIRE(metrics.flows().size() == 1);
    const auto& flow = metrics.flows().front();

    CHECK(near(flow.avgLatencyMs, 100.0));
    CHECK(flow.rttSamples == 1);
    CHECK(near(flow.avgRttMs, 250.0));
}

TEST_CASE("MetricsCollector records recovery after the link is restored") {
    netmind::Graph graph;
    netmind::MetricsCollector metrics(graph);

    auto interrupted = makePacket(30, 9, 0.9);
    metrics.onSent(0.9, interrupted);

    metrics.onNetworkEvent(
        1.0, "LINK_DOWN", "7", "state=down");
    metrics.onDropped(
        1.0,
        interrupted,
        "PC1",
        netmind::DropReason::LINK_DOWN);

    // The link returns before delivery resumes for the interrupted flow.
    metrics.onNetworkEvent(
        1.2, "LINK_UP", "7", "state=up");

    auto resumed = makePacket(31, 9, 1.4);
    metrics.onSent(1.4, resumed);
    metrics.onDelivered(1.5, resumed);

    metrics.finalize(2.0);

    // Recovery is measured from LINK_DOWN to the first resumed delivery.
    CHECK(near(metrics.network().recoveryTimeS, 0.5));
}