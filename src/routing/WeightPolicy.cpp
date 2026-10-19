#include "netmind/routing/WeightPolicy.h"

#include <cmath>

#include "netmind/common/Errors.h"

namespace netmind {

double HopCountWeight::weight(const Link&) const {
    return 1.0;
}

double LatencyWeight::weight(const Link& link) const {
    return link.latencyMs();
}

LatencyCongestionWeight::LatencyCongestionWeight(double alpha, const LinkLoadProvider* loadProvider)
    : alpha_(alpha), loadProvider_(loadProvider) {
    if (!std::isfinite(alpha_) || alpha_ < 0.0) {
        throw ConfigError("Congestion weight alpha must be finite and non-negative");
    }
}

double LatencyCongestionWeight::weight(const Link& link) const {
    const double queueDelayMs =
        loadProvider_ == nullptr ? 0.0 : loadProvider_->currentQueueDelayMs(link);

    if (!std::isfinite(queueDelayMs) || queueDelayMs < 0.0) {
        throw ConfigError("Current queue delay must be finite and non-negative");
    }

    const double result = link.latencyMs() + alpha_ * queueDelayMs;
    if (!std::isfinite(result)) {
        throw ConfigError("Congestion-adjusted link weight is not finite");
    }

    return result;
}

}