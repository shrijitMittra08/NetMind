#include "netmind/model/Flow.h"

#include "netmind/common/Errors.h"

#include <cmath>
#include <utility>

namespace netmind {

Flow::Flow(
    FlowId id,
    NodeId src,
    NodeId dst,
    SimTime startTime,
    SimTime stopTime,
    std::unique_ptr<TrafficPattern> pattern)
    : id_(id),
      src_(src),
      dst_(dst),
      startTime_(startTime),
      stopTime_(stopTime),
      pattern_(std::move(pattern)) {
    if (!std::isfinite(startTime_) || startTime_ < 0.0) {
        throw ConfigError("Flow start time must be finite and non-negative");
    }

    if (!std::isfinite(stopTime_) || stopTime_ < startTime_) {
        throw ConfigError(
            "Flow stop time must be finite and no earlier than its start time");
    }

    if (!pattern_) {
        throw ConfigError("Flow must have a traffic pattern");
    }
}

FlowId Flow::id() const {
    return id_;
}

NodeId Flow::src() const {
    return src_;
}

NodeId Flow::dst() const {
    return dst_;
}

SimTime Flow::startTime() const {
    return startTime_;
}

SimTime Flow::stopTime() const {
    return stopTime_;
}

TrafficPattern& Flow::pattern() {
    return *pattern_;
}

const TrafficPattern& Flow::pattern() const {
    return *pattern_;
}

Packet Flow::makePacket(SimTime now, PacketId packetId) {
    if (!std::isfinite(now) || now < startTime_ || now >= stopTime_) {
        throw ConfigError(
            "Packet generation time must be within the flow interval");
    }

    Packet packet;
    packet.id = packetId;
    packet.srcId = src_;
    packet.dstId = dst_;
    packet.flowId = id_;
    packet.sizeBytes = pattern_->packetSizeBytes();
    packet.timestampSent = now;
    packet.originTime = now;

    return packet;
}

}