#pragma once

#include "netmind/common/Types.h"
#include "netmind/simulation/NetworkEvent.h"

namespace netmind {

class CongestionEvent final : public NetworkEvent {
public:
    CongestionEvent(
        LinkId linkId,
        double bandwidthMultiplier,
        double extraLossRate,
        SimTime duration);

    void apply(SimulationEngine& engine) override;
    [[nodiscard]] std::string describe() const override;

private:
    LinkId linkId_;
    double bandwidthMultiplier_;
    double extraLossRate_;
    SimTime duration_;
};

}