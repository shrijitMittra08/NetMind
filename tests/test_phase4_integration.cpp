#include "netmind/SimObserver.h"
#include "netmind/common/Types.h"
#include "netmind/graph/Graph.h"
#include "netmind/graph/TopologyLoader.h"
#include "netmind/model/Flow.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Packet.h"
#include "netmind/model/Router.h"
#include "netmind/model/TrafficPattern.h"
#include "netmind/simulation/SimulationEngine.h"
#include "netmind/simulation/events/LinkFailureEvent.h"
#include "netmind/simulation/events/LinkRestoreEvent"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ForwardRecord {
    netmind::SimTime time{};
    netmind::PacketId packetId{};
    netmind::NodeId at{};
    netmind::NodeId next{};
};

struct SentRecord {
    netmind::FlowId flowId{};
    std::size_t packetBytes{};
};

struct DropRecord {
    netmind::SimTime time{};
    netmind::SimTime packetSentTime{};
    netmind::FlowId flowId{};
    netmind::DropReason reason{};
};

struct DeliveryRecord {
    netmind::SimTime time{};
    netmind::SimTime packetSentTime{};
    netmind::FlowId flowId{};
    std::size_t packetBytes{};
};

class SimulationRecorder final : public netmind::SimObserver {
public:
    void onSent(
        netmind::SimTime,
        const netmind::Packet& packet) override {
        sent.push_back({
            packet.flowId,
            packet.sizeBytes
        });
    }

    void onForward(
        netmind::SimTime time,
        const netmind::Packet& packet,
        netmind::NodeId at,
        netmind::NodeId next) override {
        forwards.push_back({
            time,
            packet.id,
            at,
            next
        });
    }

    void onDropped(
        netmind::SimTime time,
        const netmind::Packet& packet,
        const std::string&,
        netmind::DropReason reason) override {
        drops.push_back({
            time,
            packet.timestampSent,
            packet.flowId,
            reason
        });
    }

    void onDelivered(
        netmind::SimTime time,
        const netmind::Packet& packet) override {
        deliveries.push_back({
            time,
            packet.timestampSent,
            packet.flowId,
            packet.sizeBytes
        });
    }

    std::vector<SentRecord> sent;
    std::vector<ForwardRecord> forwards;
    std::vector<DropRecord> drops;
    std::vector<DeliveryRecord> deliveries;
};

std::unique_ptr<netmind::Graph> makeRoutingGraph(bool withBackupPath) {
    auto graph = std::make_unique<netmind::Graph>();

    auto source = std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1");
    source->setGatewayId(2);

    graph->addNode(std::move(source));
    graph->addNode(std::make_unique<netmind::Router>(
        2, "RouterA", "10.0.0.2"));
    graph->addNode(std::make_unique<netmind::Router>(
        3, "RouterB", "10.0.0.3"));

    if (withBackupPath) {
        graph->addNode(std::make_unique<netmind::Router>(
            4, "RouterC", "10.0.0.4"));
    }

    graph->addNode(std::make_unique<netmind::Host>(
        5, "HostB", "10.0.0.5"));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 100.0, 0.1, 0.0, 2048));
    graph->addLink(std::make_unique<netmind::Link>(
        11, 2, 3, 100.0, 0.1, 0.0, 2048));
    graph->addLink(std::make_unique<netmind::Link>(
        12, 3, 5, 100.0, 0.1, 0.0, 2048));

    if (withBackupPath) {
        graph->addLink(std::make_unique<netmind::Link>(
            13, 2, 4, 100.0, 0.1, 0.0, 2048));
        graph->addLink(std::make_unique<netmind::Link>(
            14, 4, 3, 100.0, 0.1, 0.0, 2048));
    }

    return graph;
}

std::unique_ptr<netmind::Graph> makeLossGraph() {
    auto graph = std::make_unique<netmind::Graph>();

    graph->addNode(std::make_unique<netmind::Host>(
        1, "LossSource", "10.1.0.1"));
    graph->addNode(std::make_unique<netmind::Host>(
        2, "LossDestination", "10.1.0.2"));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 1'000'000.0, 0.0, 0.20, 100'000));

    return graph;
}

std::size_t countDrops(
    const SimulationRecorder& recorder,
    netmind::DropReason reason) {
    return static_cast<std::size_t>(std::count_if(
        recorder.drops.begin(),
        recorder.drops.end(),
        [reason](const DropRecord& drop) {
            return drop.reason == reason;
        }));
}

std::size_t deliveredBytesForFlow(
    const SimulationRecorder& recorder,
    netmind::FlowId flowId,
    netmind::SimTime byTime) {
    std::size_t total = 0;

    for (const DeliveryRecord& delivery : recorder.deliveries) {
        if (delivery.flowId == flowId && delivery.time <= byTime) {
            total += delivery.packetBytes;
        }
    }

    return total;
}

}             

// Checks that traffic switches to the backup path after a link fails and nothing is lost
TEST_CASE("Traffic reroutes over a backup path after link failure") {
    constexpr netmind::SimTime failureTime = 0.0015;

    netmind::SimulationEngine engine(1234);
    SimulationRecorder recorder;

    engine.addObserver(recorder);
    engine.setGraph(makeRoutingGraph(true));

    engine.addFlow(netmind::Flow(
        7,
        1,
        5,
        0.0,
        0.006,
        std::make_unique<netmind::ConstantRate>(8.0, 1000)));

    engine.scheduleEvent(
        failureTime,
        std::make_shared<netmind::LinkFailureEvent>(11));

    engine.runUntil(0.007);

    const bool usedOriginalRouteBeforeFailure = std::any_of(
        recorder.forwards.begin(),
        recorder.forwards.end(),
        [failureTime](const ForwardRecord& forward) {
            return forward.at == 2 &&
                forward.next == 3 &&
                forward.time < failureTime;
        });

    const bool usedBackupRouteAfterFailure = std::any_of(
        recorder.forwards.begin(),
        recorder.forwards.end(),
        [failureTime](const ForwardRecord& forward) {
            return forward.at == 2 &&
                forward.next == 4 &&
                forward.time > failureTime;
        });

    const bool usedFailedRouteAfterFailure = std::any_of(
        recorder.forwards.begin(),
        recorder.forwards.end(),
        [failureTime](const ForwardRecord& forward) {
            return forward.at == 2 &&
                forward.next == 3 &&
                forward.time > failureTime;
        });

    REQUIRE(usedOriginalRouteBeforeFailure);
    REQUIRE(usedBackupRouteAfterFailure);
    REQUIRE_FALSE(usedFailedRouteAfterFailure);
    REQUIRE(recorder.deliveries.size() == 6);
    REQUIRE(countDrops(recorder, netmind::DropReason::NO_ROUTE) == 0);
}

// Checks that packets are dropped during an outage with no backup and flow again after restore
TEST_CASE("Traffic drops during an outage without a backup path and resumes after restoration") {
    constexpr netmind::SimTime failureTime = 0.0025;
    constexpr netmind::SimTime restoreTime = 0.0055;

    netmind::SimulationEngine engine(1234);
    SimulationRecorder recorder;

    engine.addObserver(recorder);
    engine.setGraph(makeRoutingGraph(false));

    engine.addFlow(netmind::Flow(
        8,
        1,
        5,
        0.0,
        0.007,
        std::make_unique<netmind::ConstantRate>(8.0, 1000)));

    engine.scheduleEvent(
        failureTime,
        std::make_shared<netmind::LinkFailureEvent>(11));
    engine.scheduleEvent(
        restoreTime,
        std::make_shared<netmind::LinkRestoreEvent>(11));

    engine.runUntil(0.008);

    const std::size_t outageRouteDrops =
        static_cast<std::size_t>(std::count_if(
            recorder.drops.begin(),
            recorder.drops.end(),
            [failureTime, restoreTime](const DropRecord& drop) {
                return drop.reason == netmind::DropReason::NO_ROUTE &&
                    drop.time >= failureTime &&
                    drop.time < restoreTime;
            }));

    const bool deliveredPacketSentAfterRestore = std::any_of(
        recorder.deliveries.begin(),
        recorder.deliveries.end(),
        [restoreTime](const DeliveryRecord& delivery) {
            return delivery.packetSentTime > restoreTime;
        });

    REQUIRE(outageRouteDrops >= 2);
    REQUIRE(deliveredPacketSentAfterRestore);
}

// Checks that random loss over 100,000 packets stays within 1% of the link's 20% loss rate
TEST_CASE("Seeded packet loss is close to the configured link loss rate") {
    constexpr std::size_t packetCount = 100'000;
    constexpr double configuredLossRate = 0.20;

    netmind::SimulationEngine engine(987654321);
    SimulationRecorder recorder;

    engine.addObserver(recorder);
    engine.setGraph(makeLossGraph());

    for (std::size_t index = 0; index < packetCount; ++index) {
        netmind::Packet packet;
        packet.id = static_cast<netmind::PacketId>(index + 1);
        packet.srcId = 1;
        packet.dstId = 2;
        packet.sizeBytes = 1000;
        packet.timestampSent = 0.0;

        engine.transmit(packet, 1, 2);
    }

    engine.run();

    const std::size_t lostPackets =
        countDrops(recorder, netmind::DropReason::RANDOM_LOSS);
    const double observedLossRate =
        static_cast<double>(lostPackets) /
        static_cast<double>(packetCount);

    REQUIRE(recorder.sent.size() == packetCount);
    REQUIRE(recorder.deliveries.size() + lostPackets == packetCount);
    REQUIRE(observedLossRate >= configuredLossRate - 0.01);
    REQUIRE(observedLossRate <= configuredLossRate + 0.01);
}

// Checks that two 30 Mbps flows share the 50 Mbps dumbbell bottleneck with the expected drops
TEST_CASE("The dumbbell topology shares its 50 Mbps bottleneck between two flows") {
    constexpr double measurementEndTime = 5.0;
    constexpr double offeredRatePerFlowMbps = 30.0;

    const std::filesystem::path topologyPath =
        std::filesystem::path(NETMIND_SOURCE_DIR) /
        "config" / "topologies" / "dumbbell.json";

    netmind::SimulationEngine engine(1234);
    SimulationRecorder recorder;

    engine.addObserver(recorder);
    engine.setGraph(std::make_unique<netmind::Graph>(
        netmind::TopologyLoader::loadFromFile(topologyPath.string())));

                                                                 
    engine.addFlow(netmind::Flow(
        1,
        1,
        5,
        0.0,
        measurementEndTime,
        std::make_unique<netmind::ConstantRate>(
            offeredRatePerFlowMbps, 1000)));

                                                           
    engine.addFlow(netmind::Flow(
        2,
        2,
        6,
        0.0,
        measurementEndTime,
        std::make_unique<netmind::ConstantRate>(
            offeredRatePerFlowMbps, 1000)));

    engine.runUntil(measurementEndTime);

    std::size_t deliveredBitsByMeasurementEnd = 0;
    for (const DeliveryRecord& delivery : recorder.deliveries) {
        if (delivery.time <= measurementEndTime) {
            deliveredBitsByMeasurementEnd += delivery.packetBytes * 8;
        }
    }

    const double deliveredMbps =
        static_cast<double>(deliveredBitsByMeasurementEnd) /
        measurementEndTime /
        1'000'000.0;

    const std::size_t queueFullDrops =
        countDrops(recorder, netmind::DropReason::QUEUE_FULL);
    const double observedDropRate =
        static_cast<double>(queueFullDrops) /
        static_cast<double>(recorder.sent.size());

                                                            
                                                          
    const double expectedDropRate =
        (2.0 * offeredRatePerFlowMbps - 50.0) /
        (2.0 * offeredRatePerFlowMbps);

    const std::size_t flow1Bytes =
        deliveredBytesForFlow(recorder, 1, measurementEndTime);
    const std::size_t flow2Bytes =
        deliveredBytesForFlow(recorder, 2, measurementEndTime);

    REQUIRE(recorder.sent.size() > 0);
    REQUIRE(queueFullDrops > 0);

                                                                       
                                                                            
    REQUIRE(deliveredMbps > 48.0);
    REQUIRE(deliveredMbps <= 50.1);

    REQUIRE(observedDropRate >= expectedDropRate - 0.015);
    REQUIRE(observedDropRate <= expectedDropRate + 0.015);

                                                                              
    REQUIRE(flow1Bytes * 8 > 20'000'000);
    REQUIRE(flow2Bytes * 8 > 20'000'000);
}