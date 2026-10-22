#include "netmind/simulation/events/CongestionEvent.h"

#include "netmind/common/Errors.h"
#include "netmind/simulation/SimulationEngine.h"

#include <cmath>
#include <string>

namespace netmind {

CongestionEvent::CongestionEvent(
    LinkId linkId,
    double bandwidthMultiplier,
    double extraLossRate,
    SimTime duration)
    : linkId_(linkId),
      bandwidthMultiplier_(bandwidthMultiplier),
      extraLossRate_(extraLossRate),
      duration_(duration) {
    if (!std::isfinite(bandwidthMultiplier_) ||
        bandwidthMultiplier_ <= 0.0 ||
        bandwidthMultiplier_ > 1.0) {
        throw ConfigError(
            "Congestion bandwidth multiplier must be in (0, 1]");
    }

    if (!std::isfinite(extraLossRate_) || extraLossRate_ < 0.0) {
        throw ConfigError(
            "Congestion extra loss must be finite and non-negative");
    }

    if (!std::isfinite(duration_) || duration_ <= 0.0) {
        throw ConfigError(
            "Congestion duration must be finite and positive");
    }
}

void CongestionEvent::apply(SimulationEngine& engine) {
    engine.applyCongestion(
        linkId_,
        bandwidthMultiplier_,
        extraLossRate_,
        duration_);
}

std::string CongestionEvent::describe() const {
    return "CONGESTION link=" + std::to_string(linkId_);
}

}