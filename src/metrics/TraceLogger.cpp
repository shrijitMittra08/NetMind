#include "netmind/metrics/TraceLogger.h"

#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace netmind {
namespace {

constexpr std::size_t FlushInterval = 4096;

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') {
            escaped += '"';
        }
        escaped += character;
    }
    return escaped + '"';
}

std::string formatTime(SimTime time) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << time;
    return output.str();
}

std::string detailValue(const std::string& detail, const std::string& key) {
    const std::size_t start = detail.find(key + "=");
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t valueStart = start + key.size() + 1;
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
    const char* end = text.data() + text.size();
    NodeId parsed{};
    const auto result = std::from_chars(text.data(), end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    id = parsed;
    return true;
}

}

TraceLogger::TraceLogger(const std::string& outputPath) : TraceLogger(outputPath, {}, {}) {}

TraceLogger::TraceLogger(const std::string& outputPath, NodeNameResolver nodeNameResolver,
                         ChannelNameResolver channelNameResolver)
    : output_(outputPath, std::ios::out | std::ios::trunc),
      nodeNameResolver_(std::move(nodeNameResolver)),
      channelNameResolver_(std::move(channelNameResolver)) {
    if (!output_.is_open()) {
        throw std::runtime_error("Cannot open trace output file: " + outputPath);
    }
    output_ << "time_s,event_type,packet_id,flow_id,location,detail\n";
    if (!output_) {
        throw std::runtime_error("Cannot write trace CSV header: " + outputPath);
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
        throw std::runtime_error("Failed to flush Project901 trace CSV");
    }
    linesSinceFlush_ = 0;
}

std::string TraceLogger::nameOf(NodeId id) const {
    return nodeNameResolver_ ? nodeNameResolver_(id) : std::to_string(id);
}

std::string TraceLogger::channelName(const ChannelId& channel) const {
    if (channelNameResolver_) {
        return channelNameResolver_(channel);
    }
    const std::string direction = channel.dir == Direction::AtoB ? "A>B" : "B>A";
    return "link_" + std::to_string(channel.linkId) + ":" + direction;
}

void TraceLogger::writeRow(const std::string& time, const std::string& eventType,
                           const std::string& packetId, const std::string& flowId,
                           const std::string& location, const std::string& detail) {
    output_ << csvEscape(time) << ',' << csvEscape(eventType) << ',' << csvEscape(packetId) << ','
            << csvEscape(flowId) << ',' << csvEscape(location) << ',' << csvEscape(detail) << '\n';
    if (!output_) {
        throw std::runtime_error("Failed to write Project901 trace CSV");
    }
    ++linesSinceFlush_;
    if (linesSinceFlush_ >= FlushInterval) {
        flush();
    }
}

void TraceLogger::writePacketRow(SimTime time, const std::string& eventType, const Packet& packet,
                                 const std::string& location, const std::string& detail) {
    writeRow(formatTime(time), eventType, std::to_string(packet.id), std::to_string(packet.flowId),
             location, detail);
}

void TraceLogger::onSent(SimTime time, const Packet& packet) {
    writePacketRow(time, "SENT", packet, nameOf(packet.srcId),
                   "dst=" + nameOf(packet.dstId) + ";size=" + std::to_string(packet.sizeBytes) +
                       ";type=" + toString(packet.type));
}

void TraceLogger::onEnqueue(SimTime time, const Packet& packet, const ChannelId& channel,
                            std::size_t queueLength) {
    writePacketRow(time, "ENQUEUE", packet, channelName(channel),
                   "qlen=" + std::to_string(queueLength));
}

void TraceLogger::onTxStart(SimTime time, const Packet& packet, const ChannelId& channel,
                            SimTime eta) {
    writePacketRow(time, "TX_START", packet, channelName(channel), "eta=" + formatTime(eta));
}

void TraceLogger::onArrive(SimTime time, const Packet& packet, NodeId node, NodeId from) {
    writePacketRow(time, "ARRIVE", packet, nameOf(node), "from=" + nameOf(from));
}

void TraceLogger::onForward(SimTime time, const Packet& packet, NodeId at, NodeId next) {
    writePacketRow(time, "FORWARD", packet, nameOf(at), "next=" + nameOf(next));
}

void TraceLogger::onDelivered(SimTime time, const Packet& packet) {
    const double latencyMs = (time - packet.timestampSent) * 1000.0;
    const std::size_t hops = packet.pathTaken.empty() ? 0 : packet.pathTaken.size() - 1;
    writePacketRow(time, "DELIVERED", packet, nameOf(packet.dstId),
                   "latency_ms=" + formatTime(latencyMs) + ";hops=" + std::to_string(hops));
}

void TraceLogger::onDropped(SimTime time, const Packet& packet, const std::string& where,
                            DropReason reason) {
    writePacketRow(time, "DROPPED", packet, where, std::string("reason=") + toString(reason));
}

void TraceLogger::onNetworkEvent(SimTime time, const std::string& type, const std::string& target,
                                 const std::string& detail) {
    if (type == "FLOW_START") {
        NodeId sourceId{};
        NodeId destinationId{};
        if (parseNodeId(detailValue(detail, "src"), sourceId) &&
            parseNodeId(detailValue(detail, "dst"), destinationId)) {
            flowTraceInfo_[target] = {nameOf(sourceId), nameOf(destinationId)};
            writeFlowRow(time, type, target);
            return;
        }
    } else if (type == "FLOW_STOP" && flowTraceInfo_.count(target) != 0) {
        writeFlowRow(time, type, target);
        return;
    }
    writeRow(formatTime(time), type, "-", "-", target, detail.empty() ? "-" : detail);
}

void TraceLogger::writeFlowRow(SimTime time, const std::string& type, const std::string& target) {
    const FlowTraceInfo& flowInfo = flowTraceInfo_.at(target);
    writeRow(formatTime(time), type, "-", target, flowInfo.sourceName,
             "dst=" + flowInfo.destinationName);
}

}
