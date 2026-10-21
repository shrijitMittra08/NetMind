#pragma once

#include "netmind/common/Types.h"
#include "netmind/simulation/NetworkEvent.h"

#include <cstdint>

namespace netmind {

class TrafficBurstEvent final : public NetworkEvent {
public:
    TrafficBurstEvent(
        FlowId flowId,
        NodeId srcId,
        NodeId dstId,
        SimTime duration,
        double rateMbps,
        std::uint32_t packetBytes);

    void apply(SimulationEngine& engine) override;
    [[nodiscard]] std::string describe() const override;

private:
    FlowId flowId_;
    NodeId srcId_;
    NodeId dstId_;
    SimTime duration_;
    double rateMbps_;
    std::uint32_t packetBytes_;
};

}