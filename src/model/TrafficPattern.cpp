#include "model/TrafficPattern.h"
#include <stdexcept>
namespace netmind {
ConstantRate::ConstantRate(double rateMbps, std::uint32_t packetBytes) {
    if (rateMbps <= 0 || packetBytes == 0) {
        throw std::invalid_argument("Rate and packet size must be greater than 0");
    }
    gap_ = (static_cast<double>(packetBytes) * 8.0) /
           (rateMbps * 1'000'000.0);
}
SimTime ConstantRate::nextInterval(Rng& rng) {
    (void)rng;
    return gap_;
}
std::unique_ptr<TrafficPattern> ConstantRate::clone() const {
    return std::make_unique<ConstantRate>(*this);
}
const char* ConstantRate::name() const {
    return "ConstantRate";
}
BurstyPattern::BurstyPattern(SimTime onSec,
                             SimTime offSec,
                             double onRateMbps,
                             std::uint32_t packetBytes)
    : onSec_(onSec),
      offSec_(offSec) {
    if (onSec <= 0 || offSec < 0 || onRateMbps <= 0 || packetBytes == 0) {
        throw std::invalid_argument("Invalid burst parameters");
    }
    gap_ = (static_cast<double>(packetBytes) * 8.0) /
           (onRateMbps * 1'000'000.0);
}
SimTime BurstyPattern::nextInterval(Rng& rng) {
    (void)rng;
    SimTime candidate = pos_ + gap_;
    if (candidate < onSec_) {
        pos_ = candidate;
        return gap_;
    }
    SimTime wait = (onSec_ + offSec_) - pos_;
    pos_ = 0.0;
    return wait;
}
std::unique_ptr<TrafficPattern> BurstyPattern::clone() const {
    return std::make_unique<BurstyPattern>(*this);
}
const char* BurstyPattern::name() const {
    return "BurstyPattern";
}
}