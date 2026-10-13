#include "netmind/metrics/MetricsCollector.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numeric>

namespace {

bool near(double actual, double expected, double tolerance = 1e-6) {
    return std::abs(actual - expected) <= tolerance;
}

netmind::Packet makePacket(
    netmind::PacketId id,
    netmind::FlowId flowId,
    netmind::SimTime sentTime) {
    netmind::Packet packet;
    packet.id = id;
    packet.flowId = flowId;
    packet.srcId = 1;
    packet.dstId = 2;
    packet.sizeBytes = 1000;
    packet.type = netmind::PacketType::DATA;
    packet.timestampSent = sentTime;
    packet.pathTaken = {1, 2};
    return packet;
}

} // namespace

TEST_CASE("MetricsCollector matches hand-calculated results and cross-checks") {
    netmind::Graph graph;
    netmind::MetricsCollector metrics(graph);

    constexpr netmind::FlowId flowId = 55;
    constexpr std::size_t packetCount = 10;

    // Seven delivered packets have known one-way latencies.
    const double latenciesMs[] = {
        10.0, 12.0, 10.0, 14.0, 16.0, 12.0, 14.0
    };

    metrics.onNetworkEvent(
        0.0, "FLOW_START", "55", "src=1;dst=2");

    for (std::size_t i = 0; i < packetCount; ++i) {
        const auto packetId =
            static_cast<netmind::PacketId>(i + 1);
        const double sentTime = 0.05 * static_cast<double>(i);
        auto packet = makePacket(packetId, flowId, sentTime);

        metrics.onSent(sentTime, packet);

        if (i < 7) {
            metrics.onDelivered(
                sentTime + latenciesMs[i] / 1000.0,
                packet);
        } else if (i < 9) {
            metrics.onDropped(
                sentTime + 0.005,
                packet,
                "PC1",
                netmind::DropReason::NO_ROUTE);
        }
        // Packet 10 remains unfinished at the end of the run.
    }

    metrics.onNetworkEvent(
        1.0, "FLOW_STOP", "55", "state=stopped");
    metrics.finalize(1.0);

    REQUIRE(metrics.flows().size() == 1);
    const auto& flow = metrics.flows().front();

    CHECK(flow.sent == 10);
    CHECK(flow.delivered == 7);
    CHECK(flow.dropped == 2);
    CHECK(flow.unfinished == 1);

    // Guide invariant: SENT = DELIVERED + DROPPED + UNFINISHED.
    CHECK(flow.sent ==
          flow.delivered + flow.dropped + flow.unfinished);

    // Loss excludes unfinished packets: 2 / (7 + 2) * 100.
    CHECK(near(flow.lossPct, 200.0 / 9.0));

    // Latencies total 88 ms across seven delivered packets.
    CHECK(near(flow.avgLatencyMs, 88.0 / 7.0));
    CHECK(near(flow.minLatencyMs, 10.0));
    CHECK(near(flow.maxLatencyMs, 16.0));
    CHECK(near(flow.p95LatencyMs, 16.0));

    // Absolute consecutive differences total 16 ms across six pairs.
    CHECK(near(flow.jitterMs, 16.0 / 6.0));
    CHECK(near(flow.avgHops, 1.0));

    // Seven 1000-byte packets delivered during a one-second flow.
    CHECK(near(flow.throughputMbps, 0.056));
    CHECK(near(flow.goodputMbps, 0.056));

    const auto windowSent = std::accumulate(
        metrics.windows().begin(),
        metrics.windows().end(),
        std::size_t{0},
        [](std::size_t total, const netmind::WindowMetrics& window) {
            return total + window.sent;
        });

    const auto windowDelivered = std::accumulate(
        metrics.windows().begin(),
        metrics.windows().end(),
        std::size_t{0},
        [](std::size_t total, const netmind::WindowMetrics& window) {
            return total + window.delivered;
        });

    const auto windowDropped = std::accumulate(
        metrics.windows().begin(),
        metrics.windows().end(),
        std::size_t{0},
        [](std::size_t total, const netmind::WindowMetrics& window) {
            return total + window.dropped;
        });

    // Guide cross-check: window totals must agree with flow totals.
    CHECK(windowSent == flow.sent);
    CHECK(windowDelivered == flow.delivered);
    CHECK(windowDropped == flow.dropped);

    CHECK(metrics.network().totalDrops == flow.dropped);
    CHECK(near(metrics.network().reliabilityPct, 700.0 / 9.0));
    CHECK(near(metrics.network().avgLatencyMs, 88.0 / 7.0));
}