#include "netmind/model/Link.h"

#include "netmind/common/Errors.h"
#include "netmind/routing/WeightPolicy.h"

#include <cmath>

namespace netmind {

Link::Link(
    LinkId id,
    NodeId a,
    NodeId b,
    double bandwidthMbps,
    double latencyMs,
    double lossRate,
    std::size_t queueCapacity)
    : id_(id),
      a_(a),
      b_(b),
      bandwidthMbps_(bandwidthMbps),
      latencyMs_(latencyMs),
      lossRate_(lossRate),
      queueCapacity_(queueCapacity) {
    if (!std::isfinite(bandwidthMbps_) || bandwidthMbps_ <= 0.0) {
        throw ConfigError("Link bandwidth must be finite and positive");
    }

    if (!std::isfinite(latencyMs_) || latencyMs_ < 0.0) {
        throw ConfigError("Link latency must be finite and non-negative");
    }

    if (!std::isfinite(lossRate_) || lossRate_ < 0.0 || lossRate_ > 1.0) {
        throw ConfigError("Link loss rate must be between 0 and 1");
    }
}

LinkId Link::getId() const {
    return id_;
}

NodeId Link::a() const {
    return a_;
}

NodeId Link::b() const {
    return b_;
}

double Link::bandwidthMbps() const {
    return bandwidthMbps_;
}

double Link::latencyMs() const {
    return latencyMs_;
}

double Link::lossRate() const {
    return lossRate_;
}

std::size_t Link::queueCapacity() const {
    return queueCapacity_;
}

bool Link::connects(NodeId x, NodeId y) const {
    return (a_ == x && b_ == y) || (a_ == y && b_ == x);
}

NodeId Link::other(NodeId endpoint) const {
    if (endpoint == a_) {
        return b_;
    }

    if (endpoint == b_) {
        return a_;
    }

    throw TopologyError("Node is not an endpoint of this link");
}

LinkState Link::state() const {
    return state_;
}

void Link::setState(LinkState state) {
    if (state_ != LinkState::DOWN && state == LinkState::DOWN) {
        ++epoch_;
    }

    state_ = state;
}

std::uint64_t Link::epoch() const {
    return epoch_;
}

double Link::degradeFactor() const {
    return degradeFactor_;
}

void Link::setDegradeFactor(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0) {
        throw ConfigError(
            "Link degradation factor must be finite and in (0, 1]");
    }

    degradeFactor_ = factor;
}

double Link::effectiveBandwidth() const {
    if (state_ == LinkState::DEGRADED) {
        return bandwidthMbps_ * degradeFactor_;
    }

    return bandwidthMbps_;
}

bool Link::isUsable() const {
    return state_ != LinkState::DOWN;
}

double Link::getWeight(const WeightPolicy& policy) const {
    return policy.weight(*this);
}

double Link::extraLossRate() const {
    return extraLossRate_;
}

double Link::effectiveLossRate() const {
    return lossRate_ + extraLossRate_;
}

void Link::setExtraLossRate(double extraLossRate) {
    if (!std::isfinite(extraLossRate) ||
        extraLossRate < 0.0 ||
        lossRate_ + extraLossRate > 1.0) {
        throw ConfigError(
            "Additional link loss must be finite and keep total loss "
            "between 0 and 1");
    }

    extraLossRate_ = extraLossRate;
}

}