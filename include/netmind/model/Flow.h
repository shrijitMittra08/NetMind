#pragma once

#include "netmind/common/Types.h"
#include "netmind/model/Packet.h"
#include "netmind/model/TrafficPattern.h"

#include <memory>

namespace netmind {

class Flow {
  public:
    Flow(FlowId id, NodeId src, NodeId dst, SimTime startTime, SimTime stopTime,
         std::unique_ptr<TrafficPattern> pattern);

    [[nodiscard]] FlowId id() const { return id_; }
    [[nodiscard]] NodeId src() const { return src_; }
    [[nodiscard]] NodeId dst() const { return dst_; }
    [[nodiscard]] SimTime startTime() const { return startTime_; }
    [[nodiscard]] SimTime stopTime() const { return stopTime_; }
    [[nodiscard]] TrafficPattern& pattern() { return *pattern_; }
    [[nodiscard]] const TrafficPattern& pattern() const { return *pattern_; }

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
