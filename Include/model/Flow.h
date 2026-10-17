#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "common/Types.h"
#include "model/Packet.h"
#include "model/TrafficPattern.h"

namespace netmind {

class Rng;

class Flow {
public:
    Flow(FlowId id,
         NodeId src,
         NodeId dst,
         std::uint32_t packetBytes,
         SimTime start,
         SimTime stop,
         std::unique_ptr<TrafficPattern> pattern);

    Packet makePacket(SimTime now, PacketId packetId);

    std::optional<SimTime> nextGenerationTime(SimTime now, Rng& rng);

    SimTime firstGenerationTime() const;

    bool isActiveAt(SimTime t) const;

    std::uint64_t generated() const;

    std::unique_ptr<Flow> clone() const;

private:
    FlowId id_;
    NodeId src_;
    NodeId dst_;
    std::uint32_t packetBytes_;
    SimTime start_;
    SimTime stop_;

    std::unique_ptr<TrafficPattern> pattern_;

    std::uint64_t generated_ = 0;
};

} 