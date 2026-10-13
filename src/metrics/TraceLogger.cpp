#include "netmind/metrics/TraceLogger.h"

#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace netmind {
namespace {

constexpr std::size_t FlushInterval = 4096;

constexpr const char* TraceHeader =
    "time_s,event_type,packet_id,flow_id,location,detail\n";

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

std::string formatTime(SimTime time) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << time;
    return output.str();
}

std::string formatDetail(
    const std::vector<std::pair<std::string, std::string>>& values) {
    if (values.empty()) {
        return "-";
    }

    std::ostringstream output;

    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ';';
        }

        output << values[index].first << '=' << values[index].second;
    }

    return output.str();
}

std::string packetIdString(const Packet& packet) {
    return std::to_string(packet.id);
}

std::string flowIdString(const Packet& packet) {
    return std::to_string(packet.flowId);
}

std::string defaultChannelName(const ChannelId& channel) {
    std::ostringstream output;
    output << "link_" << channel.linkId << ':';

    if (channel.dir == Direction::AtoB) {
        output << "A>B";
    } else {
        output << "B>A";
    }

    return output.str();
}

void writeRow(
    std::ofstream& output,
    std::size_t& linesSinceFlush,
    const std::string& time,
    const std::string& eventType,
    const std::string& packetId,
    const std::string& flowId,
    const std::string& location,
    const std::string& detail) {
    output
        << csvEscape(time) << ','
        << csvEscape(eventType) << ','
        << csvEscape(packetId) << ','
        << csvEscape(flowId) << ','
        << csvEscape(location) << ','
        << csvEscape(detail) << '\n';

    if (!output) {
        throw std::runtime_error("Failed to write NetMind trace CSV");
    }

    ++linesSinceFlush;

    if (linesSinceFlush >= FlushInterval) {
        output.flush();

        if (!output) {
            throw std::runtime_error("Failed to flush NetMind trace CSV");
        }

        linesSinceFlush = 0;
    }
}

std::string detailValue(
    const std::string& detail,
    const std::string& key) {
    const std::string prefix = key + "=";
    const std::size_t start = detail.find(prefix);

    if (start == std::string::npos) {
        return {};
    }

    const std::size_t valueStart = start + prefix.size();
    const std::size_t end = detail.find(';', valueStart);

    if (end == std::string::npos) {
        return detail.substr(valueStart);
    }

    return detail.substr(valueStart, end - valueStart);
}

bool parseNodeId(const std::string& text, NodeId& id) {
    if (text.empty()) {
        return false;
    }

    NodeId parsed{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }

    id = parsed;
    return true;
}

}

TraceLogger::TraceLogger(const std::string& outputPath)
    : TraceLogger(outputPath, {}, {}) {}

TraceLogger::TraceLogger(
    const std::string& outputPath,
    NodeNameResolver nodeNameResolver,
    ChannelNameResolver channelNameResolver)
    : output_(outputPath, std::ios::out | std::ios::trunc),
      nodeNameResolver_(std::move(nodeNameResolver)),
      channelNameResolver_(std::move(channelNameResolver)) {
    if (!output_.is_open()) {
        throw std::runtime_error(
            "Cannot open trace output file: " + outputPath);
    }

    output_ << TraceHeader;

    if (!output_) {
        throw std::runtime_error(
            "Cannot write trace CSV header: " + outputPath);
    }
}

TraceLogger::~TraceLogger() {
    if (output_.is_open()) {
        output_.flush();
    }
}

void TraceLogger::flush() {
    output_.flush();
    if (!output_) {
        throw std::runtime_error("Failed to flush NetMind trace CSV");
    }
    linesSinceFlush_ = 0;
}

std::string TraceLogger::nameOf(NodeId id) const {
    if (nodeNameResolver_) {
        return nodeNameResolver_(id);
    }

    return std::to_string(id);
}

std::string TraceLogger::channelName(const ChannelId& channel) const {
    if (channelNameResolver_) {
        return channelNameResolver_(channel);
    }

    return defaultChannelName(channel);
}

void TraceLogger::onSent(SimTime time, const Packet& packet) {
    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        "SENT",
        packetIdString(packet),
        flowIdString(packet),
        nameOf(packet.srcId),
        formatDetail({
            {"dst", nameOf(packet.dstId)},
            {"size", std::to_string(packet.sizeBytes)},
            {"type", toString(packet.type)}
        }));
}

void TraceLogger::onEnqueue(
    SimTime time,
    const Packet& packet,
    const ChannelId& channel,
    std::size_t queueLength) {
    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        "ENQUEUE",
        packetIdString(packet),
        flowIdString(packet),
        channelName(channel),
        formatDetail({
            {"qlen", std::to_string(queueLength)}
        }));
}

void TraceLogger::onTxStart(
    SimTime time,
    const Packet& packet,
    const ChannelId& channel,
    SimTime eta) {
    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        "TX_START",
        packetIdString(packet),
        flowIdString(packet),
        channelName(channel),
        formatDetail({
            {"eta", formatTime(eta)}
        }));
}

void TraceLogger::onArrive(
    SimTime time,
    const Packet& packet,
    NodeId node,
    NodeId from) {
    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        "ARRIVE",
        packetIdString(packet),
        flowIdString(packet),
        nameOf(node),
        formatDetail({
            {"from", nameOf(from)}
        }));
}

void TraceLogger::onForward(
    SimTime time,
    const Packet& packet,
    NodeId at,
    NodeId next) {
    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        "FORWARD",
        packetIdString(packet),
        flowIdString(packet),
        nameOf(at),
        formatDetail({
            {"next", nameOf(next)}
        }));
}

void TraceLogger::onDelivered(
    SimTime time,
    const Packet& packet) {
    const double latencyMs =
        (time - packet.timestampSent) * 1000.0;

    const std::size_t hops =
        packet.pathTaken.empty()
            ? 0
            : packet.pathTaken.size() - 1;

    std::ostringstream latency;
    latency << std::fixed << std::setprecision(6) << latencyMs;

    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        "DELIVERED",
        packetIdString(packet),
        flowIdString(packet),
        nameOf(packet.dstId),
        formatDetail({
            {"latency_ms", latency.str()},
            {"hops", std::to_string(hops)}
        }));
}

void TraceLogger::onDropped(
    SimTime time,
    const Packet& packet,
    const std::string& where,
    DropReason reason) {
    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        "DROPPED",
        packetIdString(packet),
        flowIdString(packet),
        where,
        formatDetail({
            {"reason", toString(reason)}
        }));
}

void TraceLogger::onNetworkEvent(
    SimTime time,
    const std::string& type,
    const std::string& target,
    const std::string& detail) {
    if (type == "FLOW_START") {
        const std::string sourceIdText = detailValue(detail, "src");
        const std::string destinationIdText = detailValue(detail, "dst");

        NodeId sourceId{};
        NodeId destinationId{};

        if (parseNodeId(sourceIdText, sourceId) &&
            parseNodeId(destinationIdText, destinationId)) {
            FlowTraceInfo flowInfo{
                nameOf(sourceId),
                nameOf(destinationId)
            };

            flowTraceInfo_[target] = flowInfo;

            writeRow(
                output_,
                linesSinceFlush_,
                formatTime(time),
                type,
                "-",
                target,
                flowInfo.sourceName,
                formatDetail({
                    {"dst", flowInfo.destinationName}
                }));
            return;
        }
    }

    if (type == "FLOW_STOP") {
        const auto flowInfo = flowTraceInfo_.find(target);

        if (flowInfo != flowTraceInfo_.end()) {
            writeRow(
                output_,
                linesSinceFlush_,
                formatTime(time),
                type,
                "-",
                target,
                flowInfo->second.sourceName,
                formatDetail({
                    {"dst", flowInfo->second.destinationName}
                }));
            return;
        }
    }

    writeRow(
        output_,
        linesSinceFlush_,
        formatTime(time),
        type,
        "-",
        "-",
        target,
        detail.empty() ? "-" : detail);
}

}
