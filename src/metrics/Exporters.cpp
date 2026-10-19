#include "netmind/metrics/Exporters.h"

#include "netmind/metrics/MetricsCollector.h"

#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>

namespace netmind {
namespace {

std::ofstream openOutput(const std::filesystem::path& path) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "Could not open output file: " + path.string());
    }

    return output;
}

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }

    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

std::string number(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << value;
    return output.str();
}

void writeRow(
    std::ofstream& output,
    std::initializer_list<std::string> fields) {
    bool first = true;
    for (const auto& field : fields) {
        if (!first) {
            output << ',';
        }
        output << csvEscape(field);
        first = false;
    }
    output << '\n';

    if (!output) {
        throw std::runtime_error("Failed while writing CSV output");
    }
}

void validateRunId(const std::string& runId) {
    if (runId.empty() ||
        runId == "." ||
        runId == ".." ||
        runId.find_first_of("/\\") != std::string::npos) {
        throw std::invalid_argument(
            "Run ID must be a non-empty filename component");
    }
}

}

void Exporters::exportFlowsCsv(
    const std::filesystem::path& outputPath,
    const MetricsCollector& metrics) {
    auto output = openOutput(outputPath);

    output << "flow_id,src,dst,sent,delivered,dropped,loss_pct,"
              "throughput_mbps,goodput_mbps,avg_latency_ms,min,max,p95,"
              "jitter_ms,avg_hops\n";

    for (const auto& flow : metrics.flows()) {
        writeRow(output, {
            std::to_string(flow.flowId),
            std::to_string(flow.srcId),
            std::to_string(flow.dstId),
            std::to_string(flow.sent),
            std::to_string(flow.delivered),
            std::to_string(flow.dropped),
            number(flow.lossPct),
            number(flow.throughputMbps),
            number(flow.goodputMbps),
            number(flow.avgLatencyMs),
            number(flow.minLatencyMs),
            number(flow.maxLatencyMs),
            number(flow.p95LatencyMs),
            number(flow.jitterMs),
            number(flow.avgHops)
        });
    }
}

void Exporters::exportLinksCsv(
    const std::filesystem::path& outputPath,
    const MetricsCollector& metrics) {
    auto output = openOutput(outputPath);

    output << "channel,utilization_pct,avg_queue,max_queue,tx_pkts,"
              "drops_queue,drops_loss,drops_down,downtime_s\n";

    for (const auto& channel : metrics.channels()) {
        writeRow(output, {
            channel.name,
            number(channel.utilizationPct),
            number(channel.avgQueue),
            std::to_string(channel.maxQueue),
            std::to_string(channel.txPackets),
            std::to_string(channel.dropsQueue),
            std::to_string(channel.dropsLoss),
            std::to_string(channel.dropsDown),
            number(channel.downtimeS)
        });
    }
}

void Exporters::exportWindowsCsv(
    const std::filesystem::path& outputPath,
    const MetricsCollector& metrics) {
    auto output = openOutput(outputPath);

    output << "t_start,sent,delivered,dropped,loss_pct,avg_latency_ms,"
              "jitter_ms,throughput_mbps,max_queue,max_util_pct,"
              "active_failures\n";

    for (const auto& window : metrics.windows()) {
        writeRow(output, {
            number(window.start),
            std::to_string(window.sent),
            std::to_string(window.delivered),
            std::to_string(window.dropped),
            number(window.lossPct),
            number(window.avgLatencyMs),
            number(window.jitterMs),
            number(window.throughputMbps),
            std::to_string(window.maxQueue),
            number(window.maxUtilizationPct),
            std::to_string(window.activeFailures)
        });
    }
}

void Exporters::exportNetworkCsv(
    const std::filesystem::path& outputPath,
    const MetricsCollector& metrics) {
    auto output = openOutput(outputPath);
    const auto& network = metrics.network();

    output << "reliability_pct,avg_throughput_mbps,avg_latency_ms,"
              "total_drops,recovery_time_s,events\n";

    writeRow(output, {
        number(network.reliabilityPct),
        number(network.avgThroughputMbps),
        number(network.avgLatencyMs),
        std::to_string(network.totalDrops),
        number(network.recoveryTimeS),
        std::to_string(network.events)
    });
}

void Exporters::exportManifest(
    const std::filesystem::path& outputPath,
    const std::string& manifestJson) {
    auto output = openOutput(outputPath);
    output << manifestJson;

    if (manifestJson.empty() || manifestJson.back() != '\n') {
        output << '\n';
    }

    if (!output) {
        throw std::runtime_error(
            "Failed while writing manifest: " + outputPath.string());
    }
}

void Exporters::exportAll(
    const std::filesystem::path& outputDir,
    const std::string& runId,
    const MetricsCollector& metrics,
    const std::string& manifestJson) {
    validateRunId(runId);
    std::filesystem::create_directories(outputDir);

    exportManifest(
        outputDir / (runId + ".json"),
        manifestJson);
    exportFlowsCsv(
        outputDir / (runId + "_flows.csv"),
        metrics);
    exportLinksCsv(
        outputDir / (runId + "_links.csv"),
        metrics);
    exportWindowsCsv(
        outputDir / (runId + "_windows.csv"),
        metrics);
    exportNetworkCsv(
        outputDir / (runId + "_network.csv"),
        metrics);
}

}