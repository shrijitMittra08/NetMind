#include "netmind/metrics/MetricsCollector.h"
#include "netmind/metrics/Report.h"

#include "netmind/model/Host.h"
#include "netmind/model/Link.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

namespace {

bool near(double actual, double expected, double tolerance = 1e-9) {
    return std::abs(actual - expected) <= tolerance;
}

netmind::Packet makePacket(
    netmind::PacketId packetId,
    netmind::FlowId flowId,
    netmind::NodeId destination,
    double sentTime,
    std::uint32_t sizeBytes = 1000) {
    netmind::Packet packet;
    packet.id = packetId;
    packet.flowId = flowId;
    packet.srcId = 1;
    packet.dstId = destination;
    packet.sizeBytes = sizeBytes;
    packet.timestampSent = sentTime;
    packet.pathTaken = {1, destination};
    return packet;
}

netmind::Graph makeReportGraph() {
    netmind::Graph graph;
    graph.addNode(std::make_unique<netmind::Host>(1, "PC1", "10.0.0.1"));
    graph.addNode(std::make_unique<netmind::Host>(2, "PC2", "10.0.0.2"));
    graph.addNode(std::make_unique<netmind::Host>(3, "PC3", "10.0.0.3"));

    graph.addLink(std::make_unique<netmind::Link>(
        7, 1, 2, 10.0, 1.0, 0.0, 8));
    graph.addLink(std::make_unique<netmind::Link>(
        8, 1, 3, 20.0, 1.0, 0.0, 8));

    return graph;
}

} // namespace

TEST_CASE("Report sorts links and flows deterministically") {
    auto graph = makeReportGraph();
    netmind::MetricsCollector metrics(graph);

    const netmind::ChannelId pc1ToPc2{
        7,
        netmind::Direction::AtoB};
    const netmind::ChannelId pc1ToPc3{
        8,
        netmind::Direction::AtoB};

    auto txOnFirstLink = makePacket(1, 90, 2, 0.0);
    auto txOnSecondLink = makePacket(2, 90, 3, 0.0);

    metrics.onTxStart(0.0, txOnFirstLink, pc1ToPc2, 0.001);
    metrics.onTxStart(0.0, txOnSecondLink, pc1ToPc3, 0.001);

    auto firstDrop = makePacket(3, 90, 2, 0.0);
    auto secondDrop = makePacket(4, 90, 2, 0.0);
    auto thirdDrop = makePacket(5, 90, 3, 0.0);

    metrics.onEnqueue(0.0, firstDrop, pc1ToPc2, 1);
    metrics.onDropped(
        0.1, firstDrop, "PC1", netmind::DropReason::QUEUE_FULL);

    metrics.onEnqueue(0.2, secondDrop, pc1ToPc2, 1);
    metrics.onDropped(
        0.3, secondDrop, "PC1", netmind::DropReason::RANDOM_LOSS);

    metrics.onEnqueue(0.4, thirdDrop, pc1ToPc3, 1);
    metrics.onDropped(
        0.5, thirdDrop, "PC1", netmind::DropReason::LINK_DOWN);

    auto flow1 = makePacket(11, 1, 2, 0.0);
    auto flow2 = makePacket(12, 2, 2, 0.0);
    auto flow3 = makePacket(13, 3, 3, 0.0);

    metrics.onSent(0.0, flow1);
    metrics.onDelivered(0.020, flow1);

    metrics.onSent(0.0, flow2);
    metrics.onDelivered(0.010, flow2);

    metrics.onSent(0.0, flow3);
    metrics.onDelivered(0.010, flow3);

    metrics.finalize(1.0);

    const netmind::Report report(metrics);

    SECTION("top links by utilization use metric descending") {
        const auto top = report.topLinksByUtilization(1);

        REQUIRE(top.size() == 1);
        CHECK(top[0].name == "PC1>PC2");
        CHECK(near(top[0].metric, 0.08));
    }

    SECTION("top links by drops sum the drop reasons") {
        const auto top = report.topLinksByDrops(2);

        REQUIRE(top.size() == 2);
        CHECK(top[0].name == "PC1>PC2");
        CHECK(near(top[0].metric, 2.0));
        CHECK(top[1].name == "PC1>PC3");
        CHECK(near(top[1].metric, 1.0));
    }

    SECTION("flows are sorted by latency with ties broken by name") {
        const auto sorted = report.flowsByLatency();

        REQUIRE(sorted.size() == 3);
        CHECK(sorted[0].name == "Flow 1");
        CHECK(near(sorted[0].metric, 20.0));
        CHECK(sorted[1].name == "Flow 2");
        CHECK(near(sorted[1].metric, 10.0));
        CHECK(sorted[2].name == "Flow 3");
        CHECK(near(sorted[2].metric, 10.0));
    }

    SECTION("top-k accepts values larger than the result set") {
        const auto top = report.topLinksByUtilization(100);
        CHECK(top.size() == 2);
    }
}