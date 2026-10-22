#pragma once

#include "netmind/common/Types.h"
#include "netmind/model/Packet.h"
#include "netmind/model/TrafficPattern.h"

#include <memory>

namespace netmind {

class Flow {
public:
    Flow(
        FlowId id,
        NodeId src,
        NodeId dst,
        SimTime startTime,
        SimTime stopTime,
        std::unique_ptr<TrafficPattern> pattern);

    [[nodiscard]] FlowId id() const;
    [[nodiscard]] NodeId src() const;
    [[nodiscard]] NodeId dst() const;
    [[nodiscard]] SimTime startTime() const;
    [[nodiscard]] SimTime stopTime() const;

    [[nodiscard]] TrafficPattern& pattern();
    [[nodiscard]] const TrafficPattern& pattern() const;

    [[nodiscard]] Packet makePacket(SimTime now, PacketId packetId);

private:
    FlowId id_;
    NodeId src_;
    NodeId dst_;
    SimTime startTime_;
    SimTime stopTime_;
    std::unique_ptr<TrafficPattern> pattern_;
};

}