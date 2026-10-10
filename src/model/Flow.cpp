#include "model/Flow.h"
#include <stdexcept>
#include <utility>
namespace netmind {
Flow::Flow(FlowId id,
           NodeId src,
           NodeId dst,
           std::uint32_t packetBytes,
           SimTime start,
           SimTime stop,
           std::unique_ptr<TrafficPattern> pattern)
    : id_(id),
      src_(src),
      dst_(dst),
      packetBytes_(packetBytes),
      start_(start),
      stop_(stop),
      pattern_(std::move(pattern)) {
    if (start < 0) {
        throw std::invalid_argument("Flow start time cannot be negative");
    }
    if (stop <= start) {
        throw std::invalid_argument("Flow stop time must be greater than start time");
    }
    if (packetBytes == 0) {
        throw std::invalid_argument("Packet size must be greater than 0");
    }
    if (!pattern_) {
        throw std::invalid_argument("Traffic pattern cannot be null");
    }
}
Packet Flow::makePacket(SimTime now, PacketId packetId) {
    Packet packet(packetId,
                  src_,
                  dst_,
                  id_,
                  packetBytes_,
                  PacketType::DATA,
                  64,
                  now);
    ++generated_;
    return packet;
}
std::optional<SimTime> Flow::nextGenerationTime(SimTime now, Rng& rng) {
    SimTime next = now + pattern_->nextInterval(rng);
    if (next >= stop_) {
        return std::nullopt;
    }
    return next;
}
SimTime Flow::firstGenerationTime() const {
    return start_;
}
bool Flow::isActiveAt(SimTime t) const {
    return t >= start_ && t < stop_;
}
std::uint64_t Flow::generated() const {
    return generated_;
}
std::unique_ptr<Flow> Flow::clone() const {
    auto copy = std::make_unique<Flow>(
        id_,
        src_,
        dst_,
        packetBytes_,
        start_,
        stop_,
        pattern_->clone());
    copy->generated_ = generated_;
    return copy;
}
}