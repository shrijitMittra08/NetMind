#pragma once

#include "netmind/common/Types.h"
#include "netmind/model/NetworkEntity.h"

#include <cstddef>
#include <cstdint>

namespace netmind {

class WeightPolicy;

class Link : public NetworkEntity {
  public:
    Link(LinkId id, NodeId a, NodeId b, double bandwidthMbps, double latencyMs, double lossRate,
         std::size_t queueCapacity);

    [[nodiscard]] LinkId getId() const override { return id_; }
    [[nodiscard]] NodeId a() const { return a_; }
    [[nodiscard]] NodeId b() const { return b_; }
    [[nodiscard]] double bandwidthMbps() const { return bandwidthMbps_; }
    [[nodiscard]] double latencyMs() const { return latencyMs_; }
    [[nodiscard]] double lossRate() const { return lossRate_; }
    [[nodiscard]] std::size_t queueCapacity() const { return queueCapacity_; }
    [[nodiscard]] double effectiveLossRate() const { return lossRate_ + extraLossRate_; }
    void setExtraLossRate(double extraLossRate);

    [[nodiscard]] bool connects(NodeId x, NodeId y) const;
    [[nodiscard]] NodeId other(NodeId endpoint) const;

    [[nodiscard]] LinkState state() const { return state_; }
    void setState(LinkState state);
    [[nodiscard]] std::uint64_t epoch() const { return epoch_; }
    void setDegradeFactor(double factor);

    [[nodiscard]] double effectiveBandwidth() const;
    [[nodiscard]] bool isUsable() const { return state_ != LinkState::DOWN; }
    [[nodiscard]] double getWeight(const WeightPolicy& policy) const;

  private:
    LinkId id_;
    NodeId a_;
    NodeId b_;
    double bandwidthMbps_;
    double latencyMs_;
    double lossRate_;
    std::size_t queueCapacity_;
    LinkState state_{LinkState::UP};
    std::uint64_t epoch_{};
    double degradeFactor_{1.0};
    double extraLossRate_{0.0};
};

}
