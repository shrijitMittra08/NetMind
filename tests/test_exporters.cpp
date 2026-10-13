#include "netmind/metrics/Exporters.h"
#include "netmind/metrics/MetricsCollector.h"

#include "netmind/model/Host.h"
#include "netmind/model/Link.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace {

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());

    std::string raw;
    for (std::istreambuf_iterator<char> it(input), end; it != end; ++it) {
        raw.push_back(*it);
    }

    std::string normalized;
    normalized.reserve(raw.size());

    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r' &&
            i + 1 < raw.size() &&
            raw[i + 1] == '\n') {
            normalized += '\n';
            ++i;
        } else {
            normalized += raw[i];
        }
    }

    return normalized;
}

netmind::Graph makeGraph() {
    netmind::Graph graph;
    graph.addNode(std::make_unique<netmind::Host>(
        1, "PC,1", "10.0.0.1"));
    graph.addNode(std::make_unique<netmind::Host>(
        2, "PC2", "10.0.0.2"));
    graph.addLink(std::make_unique<netmind::Link>(
        7, 1, 2, 10.0, 2.0, 0.0, 8));
    return graph;
}

} // namespace

TEST_CASE("Exporters write metric files using the agreed schemas") {
    const auto uniqueSuffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto outputDir =
        std::filesystem::temp_directory_path() /
        ("netmind_exporters_test_" + std::to_string(uniqueSuffix));

    auto graph = makeGraph();
    netmind::MetricsCollector metrics(graph);

    metrics.onNetworkEvent(
        0.0, "FLOW_START", "3", "src=1;dst=2");

    netmind::Packet packet;
    packet.id = 1;
    packet.flowId = 3;
    packet.srcId = 1;
    packet.dstId = 2;
    packet.sizeBytes = 1000;
    packet.timestampSent = 0.1;
    packet.pathTaken = {1, 2};

    const netmind::ChannelId channel{
        7,
        netmind::Direction::AtoB};

    metrics.onSent(0.1, packet);
    metrics.onEnqueue(0.1, packet, channel, 1);
    metrics.onTxStart(0.1, packet, channel, 0.1008);
    metrics.onQueueChanged(0.1008, channel, 0);
    metrics.onDelivered(0.11, packet);

    metrics.onNetworkEvent(
        1.0, "FLOW_STOP", "3", "state=stopped");
    metrics.finalize(1.0);

    netmind::Exporters::exportAll(
        outputDir,
        "run_001",
        metrics,
        R"({"seed":42,"algorithm":"dijkstra"})");

    const auto flowsPath = outputDir / "run_001_flows.csv";
    const auto linksPath = outputDir / "run_001_links.csv";
    const auto windowsPath = outputDir / "run_001_windows.csv";
    const auto networkPath = outputDir / "run_001_network.csv";
    const auto manifestPath = outputDir / "run_001.json";

    REQUIRE(std::filesystem::exists(flowsPath));
    REQUIRE(std::filesystem::exists(linksPath));
    REQUIRE(std::filesystem::exists(windowsPath));
    REQUIRE(std::filesystem::exists(networkPath));
    REQUIRE(std::filesystem::exists(manifestPath));

    const auto flows = readText(flowsPath);
    CHECK(flows.find(
        "flow_id,src,dst,sent,delivered,dropped,loss_pct,"
        "throughput_mbps,goodput_mbps,avg_latency_ms,min,max,p95,"
        "jitter_ms,avg_hops\n") == 0);
    CHECK(flows.find(
        "3,1,2,1,1,0,0.000000,0.008000,0.008000,"
        "10.000000,10.000000,10.000000,10.000000,0.000000,1.000000")
        != std::string::npos);

    const auto links = readText(linksPath);
    CHECK(links.find(
        "channel,utilization_pct,avg_queue,max_queue,tx_pkts,"
        "drops_queue,drops_loss,drops_down,downtime_s\n") == 0);
    CHECK(links.find("\"PC,1>PC2\"") != std::string::npos);
    CHECK(links.find("0.080000") != std::string::npos);

    const auto windows = readText(windowsPath);
    CHECK(windows.find(
        "t_start,sent,delivered,dropped,loss_pct,avg_latency_ms,"
        "jitter_ms,throughput_mbps,max_queue,max_util_pct,"
        "active_failures\n") == 0);
    CHECK(windows.find("0.000000,1,1,0") != std::string::npos);

    const auto network = readText(networkPath);
    CHECK(network.find(
        "reliability_pct,avg_throughput_mbps,avg_latency_ms,"
        "total_drops,recovery_time_s,events\n") == 0);
    CHECK(network.find("100.000000,0.008000,10.000000,0") !=
          std::string::npos);

    const auto manifest = readText(manifestPath);
    CHECK(manifest.find(R"({"seed":42,"algorithm":"dijkstra"})") == 0);

    std::filesystem::remove_all(outputDir);
}