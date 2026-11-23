#include "netmind/model/TrafficPattern.h"

#include "netmind/common/Errors.h"
#include "netmind/common/Units.h"

#include <cmath>

namespace netmind {
namespace {

void requirePositivePacketSize(std::uint32_t packetBytes) {
    if (packetBytes == 0) {
        throw ConfigError("Packet size must be positive");
    }
}

}

ConstantRate::ConstantRate(double rateMbps, std::uint32_t packetBytes)
    : rateMbps_(rateMbps), packetBytes_(packetBytes) {
    if (!std::isfinite(rateMbps_) || rateMbps_ <= 0.0) {
        throw ConfigError("Traffic rate must be finite and positive");
    }
    requirePositivePacketSize(packetBytes_);
}

SimTime ConstantRate::nextInterval(Rng&) {
    return txTimeSec(packetBytes_, rateMbps_);
}

BurstyPattern::BurstyPattern(SimTime onDuration, SimTime offDuration, double onRateMbps,
                             std::uint32_t packetBytes)
    : onDuration_(onDuration), offDuration_(offDuration), packetInterval_(0.0),
      packetBytes_(packetBytes) {
    if (!std::isfinite(onDuration_) || onDuration_ <= 0.0) {
        throw ConfigError("Burst on-duration must be finite and positive");
    }
    if (!std::isfinite(offDuration_) || offDuration_ < 0.0) {
        throw ConfigError("Burst off-duration must be finite and non-negative");
    }
    if (!std::isfinite(onRateMbps) || onRateMbps <= 0.0) {
        throw ConfigError("Burst rate must be finite and positive");
    }
    requirePositivePacketSize(packetBytes_);
    packetInterval_ = txTimeSec(packetBytes_, onRateMbps);
    if (packetInterval_ > onDuration_) {
        throw ConfigError("Burst on-duration must allow at least one packet interval");
    }
}

SimTime BurstyPattern::nextInterval(Rng&) {
    if (elapsedInOnPeriod_ + packetInterval_ <= onDuration_) {
        elapsedInOnPeriod_ += packetInterval_;
        return packetInterval_;
    }
    const SimTime remainingOnTime = onDuration_ - elapsedInOnPeriod_;
    elapsedInOnPeriod_ = packetInterval_;
    return remainingOnTime + offDuration_ + packetInterval_;
}

PoissonPattern::PoissonPattern(double packetsPerSecond, std::uint32_t packetBytes)
    : packetsPerSecond_(packetsPerSecond), packetBytes_(packetBytes) {
    if (!std::isfinite(packetsPerSecond_) || packetsPerSecond_ <= 0.0) {
        throw ConfigError("Poisson packet rate must be finite and positive");
    }
    requirePositivePacketSize(packetBytes_);
}

SimTime PoissonPattern::nextInterval(Rng& rng) {
    return rng.exponential(packetsPerSecond_);
}

}
