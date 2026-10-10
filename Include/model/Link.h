#pragma once
#include <string>
#include "common/Types.h"

namespace netmind {

class Link {
public:
    Link(LinkId id,
         NodeId nodeA,
         NodeId nodeB,
         double bandwidthMbps,
         double latencyMs,
         double lossRate,
         std::size_t queueCapacity);

    NodeId other(NodeId nodeId) const;
    bool connects(NodeId a, NodeId b) const;

    double effectiveBandwidth() const;
    bool isStateUp() const;
    double getWeight() const;

    void setState(LinkState state);

    Direction dirFrom(NodeId from) const;

    std::uint64_t epoch() const;

    void setDegradation(double factor);
    void clearDegradation();

    double effectiveLoss() const;
    double txDelaySec(std::size_t bytes) const;
    double propDelaySec() const;

    std::string describe() const;

private:
    LinkId id_;
    NodeId nodeA_;
    NodeId nodeB_;
    double bandwidthMbps_;
    double latencyMs_;
    double lossRate_;
    std::size_t queueCapacity_;

    LinkState state_ = LinkState::UP;
    std::uint64_t epoch_ = 0;
    double degradeFactor_ = 1.0;
};

}  // namespace netmind