#include "netmind/simulation/events/TrafficBurstEvent.h"

#include "netmind/common/Errors.h"
#include "netmind/model/Flow.h"
#include "netmind/model/TrafficPattern.h"
#include "netmind/simulation/SimulationEngine.h"

#include <cmath>
#include <memory>
#include <string>

namespace netmind {

TrafficBurstEvent::TrafficBurstEvent(
    FlowId flowId,
    NodeId srcId,
    NodeId dstId,
    SimTime duration,
    double rateMbps,
    std::uint32_t packetBytes)
    : flowId_(flowId),
      srcId_(srcId),
      dstId_(dstId),
      duration_(duration),
      rateMbps_(rateMbps),
      packetBytes_(packetBytes) {
    if (!std::isfinite(duration_) || duration_ <= 0.0) {
        throw ConfigError(
            "Traffic burst duration must be finite and positive");
    }

    if (!std::isfinite(rateMbps_) || rateMbps_ <= 0.0) {
        throw ConfigError(
            "Traffic burst rate must be finite and positive");
    }

    if (packetBytes_ == 0) {
        throw ConfigError("Traffic burst packet size must be positive");
    }
}

void TrafficBurstEvent::apply(SimulationEngine& engine) {
    const SimTime startTime = engine.now();
    const SimTime stopTime = startTime + duration_;

    auto pattern = std::make_unique<BurstyPattern>(
        duration_,
        0.0,
        rateMbps_,
        packetBytes_);

    engine.addFlow(Flow(
        flowId_,
        srcId_,
        dstId_,
        startTime,
        stopTime,
        std::move(pattern)));
}

std::string TrafficBurstEvent::describe() const {
    return "TRAFFIC_BURST flow=" + std::to_string(flowId_) +
        " src=" + std::to_string(srcId_) +
        " dst=" + std::to_string(dstId_);
}

}