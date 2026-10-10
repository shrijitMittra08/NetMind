#include "model/Link.h"
#include <stdexcept>
namespace netmind {
Link::Link(LinkId id,
           NodeId nodeA,
           NodeId nodeB,
           double bandwidthMbps,
           double latencyMs,
           double lossRate,
           std::size_t queueCapacity)
    : id_(id),
      nodeA_(nodeA),
      nodeB_(nodeB),
      bandwidthMbps_(bandwidthMbps),
      latencyMs_(latencyMs),
      lossRate_(lossRate),
      queueCapacity_(queueCapacity) {
    if (nodeA == nodeB) {
        throw std::invalid_argument("Link endpoints cannot be the same");
    }
    if (bandwidthMbps <= 0) {
        throw std::invalid_argument("Bandwidth must be greater than 0");
    }
    if (latencyMs < 0) {
        throw std::invalid_argument("Latency cannot be negative");
    }
    if (lossRate < 0 || lossRate > 1) {
        throw std::invalid_argument("Loss rate must be between 0 and 1");
    }
    if (queueCapacity == 0) {
        throw std::invalid_argument("Queue capacity must be at least 1");
    }
}
NodeId Link::other(NodeId nodeId) const {
    if (nodeId == nodeA_) {
        return nodeB_;
   }
    if (nodeId == nodeB_) {
        return nodeA_;
    }
    throw std::invalid_argument("Node is not connected to this link");
}
bool Link::connects(NodeId a, NodeId b) const {
    return (a == nodeA_ && b == nodeB_) ||
           (a == nodeB_ && b == nodeA_);
}
double Link::effectiveBandwidth() const {
    return bandwidthMbps_ * degradeFactor_;
}
bool Link::isStateUp() const {
    return state_ != LinkState::DOWN;
}
double Link::getWeight() const {
    return latencyMs_;
}
void Link::setState(LinkState state) {
    if (state == LinkState::DOWN && state_ != LinkState::DOWN) {
        ++epoch_;
    }
    state_ = state;
}
}