#pragma once

#include "netmind/common/Types.h"
#include "netmind/model/NetworkEntity.h"

#include <cstddef>
#include <cstdint>

namespace netmind {

class WeightPolicy;

class Link : public NetworkEntity {
public:
    Link(
        LinkId id,
        NodeId a,
        NodeId b,
        double bandwidthMbps,
        double latencyMs,
        double lossRate,
        std::size_t queueCapacity);

    [[nodiscard]] LinkId getId() const override;
    [[nodiscard]] NodeId a() const;
    [[nodiscard]] NodeId b() const;
    [[nodiscard]] double bandwidthMbps() const;
    [[nodiscard]] double latencyMs() const;
    [[nodiscard]] double lossRate() const;
    [[nodiscard]] std::size_t queueCapacity() const;

    [[nodiscard]] double extraLossRate() const;
    [[nodiscard]] double effectiveLossRate() const;
    void setExtraLossRate(double extraLossRate);

    [[nodiscard]] bool connects(NodeId x, NodeId y) const;
    [[nodiscard]] NodeId other(NodeId endpoint) const;

    [[nodiscard]] LinkState state() const;
    void setState(LinkState state);

    [[nodiscard]] std::uint64_t epoch() const;
    [[nodiscard]] double degradeFactor() const;
    void setDegradeFactor(double factor);

    [[nodiscard]] double effectiveBandwidth() const;
    [[nodiscard]] bool isUsable() const;
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