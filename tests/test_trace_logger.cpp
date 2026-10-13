#include "netmind/metrics/TraceLogger.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE(input.good());

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string resolveNodeName(netmind::NodeId id) {
    switch (id) {
        case 1: return "PC1";
        case 2: return "R1";
        case 3: return "R2";
        case 4: return "PC2";
        default: return "unknown";
    }
}

std::string resolveChannelName(const netmind::ChannelId& channel) {
    if (channel.linkId == 10 &&
        channel.dir == netmind::Direction::AtoB) {
        return "PC1>R1";
    }

    if (channel.linkId == 11 &&
        channel.dir == netmind::Direction::AtoB) {
        return "R1>R2";
    }

    if (channel.linkId == 12 &&
        channel.dir == netmind::Direction::AtoB) {
        return "R2>PC2";
    }

    return "unknown-channel";
}

} // namespace

TEST_CASE("TraceLogger output matches the golden trace") {
    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() /
        "netmind_trace_logger_golden_test.csv";

    const std::filesystem::path goldenPath =
        std::filesystem::path(NETMIND_SOURCE_DIR) /
        "tests" / "golden" / "mini_trace.csv";

    netmind::Packet firstPacket;
    firstPacket.id = 1001;
    firstPacket.flowId = 1;
    firstPacket.srcId = 1;
    firstPacket.dstId = 4;
    firstPacket.sizeBytes = 1000;
    firstPacket.timestampSent = 0.0;
    firstPacket.type = netmind::PacketType::DATA;
    firstPacket.pathTaken = {1, 2, 3, 4};

    netmind::Packet secondPacket = firstPacket;
    secondPacket.id = 1002;
    secondPacket.timestampSent = 0.001;

    {
        netmind::TraceLogger logger(
            outputPath.string(),
            resolveNodeName,
            resolveChannelName);

        const netmind::ChannelId pc1ToR1{
            10, netmind::Direction::AtoB};
        const netmind::ChannelId r1ToR2{
            11, netmind::Direction::AtoB};
        const netmind::ChannelId r2ToPc2{
            12, netmind::Direction::AtoB};

        // These are the values passed by SimulationEngine::startFlow.
        logger.onNetworkEvent(
            0.0, "FLOW_START", "1", "src=1;dst=4");
        logger.onSent(0.0, firstPacket);
        logger.onEnqueue(0.0, firstPacket, pc1ToR1, 1);
        logger.onTxStart(0.0, firstPacket, pc1ToR1, 0.008);

        logger.onSent(0.001, secondPacket);
        logger.onDropped(
            0.001,
            secondPacket,
            "PC1>R1",
            netmind::DropReason::QUEUE_FULL);

        logger.onArrive(0.008, firstPacket, 2, 1);
        logger.onForward(0.008, firstPacket, 2, 3);
        logger.onEnqueue(0.008, firstPacket, r1ToR2, 1);
        logger.onTxStart(0.008, firstPacket, r1ToR2, 0.016);

        logger.onNetworkEvent(
            0.010,
            "LINK_DEGRADED",
            "R1-R2",
            "bw_factor=0.5;loss=0.0");
        logger.onNetworkEvent(
            0.012,
            "LINK_DEGRADED",
            "R1-R2",
            "bw_factor=1.0;loss=0.0");

        logger.onArrive(0.016, firstPacket, 3, 2);
        logger.onForward(0.016, firstPacket, 3, 4);
        logger.onEnqueue(0.016, firstPacket, r2ToPc2, 1);
        logger.onTxStart(0.016, firstPacket, r2ToPc2, 0.024);

        logger.onNetworkEvent(
            0.020, "LINK_DOWN", "R1-R2", "");
        logger.onNetworkEvent(
            0.020,
            "ROUTE_UPDATE",
            "R1",
            "algo=dijkstra;changed=true");
        logger.onNetworkEvent(
            0.020,
            "ROUTE_UPDATE",
            "R2",
            "algo=dijkstra;changed=true");

        logger.onArrive(0.024, firstPacket, 4, 3);
        logger.onDelivered(0.024, firstPacket);

        logger.onNetworkEvent(
            0.030, "LINK_UP", "R1-R2", "");
        logger.onNetworkEvent(
            0.030,
            "ROUTE_UPDATE",
            "R1",
            "algo=dijkstra;changed=true");
        logger.onNetworkEvent(
            0.030,
            "ROUTE_UPDATE",
            "R2",
            "algo=dijkstra;changed=true");

        // SimulationEngine::stopFlow sends the flow ID and state detail.
        logger.onNetworkEvent(
            0.060, "FLOW_STOP", "1", "state=stopped");
    }

    REQUIRE(readFile(outputPath) == readFile(goldenPath));

    std::filesystem::remove(outputPath);
}