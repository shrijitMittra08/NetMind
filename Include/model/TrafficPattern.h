#pragma once
#include <memory>
#include "common/Types.h"

namespace netmind {

class Rng;

class TrafficPattern {
public:
    virtual ~TrafficPattern() = default;

    virtual SimTime nextInterval(Rng& rng) = 0;
    virtual std::unique_ptr<TrafficPattern> clone() const = 0;
    virtual const char* name() const = 0;
};

class ConstantRate : public TrafficPattern {
public:
    ConstantRate(double rateMbps, std::uint32_t packetBytes);

    SimTime nextInterval(Rng& rng) override;
    std::unique_ptr<TrafficPattern> clone() const override;
    const char* name() const override;

private:
    SimTime gap_;
};

class BurstyPattern : public TrafficPattern {
public:
    BurstyPattern(SimTime onSec,
                  SimTime offSec,
                  double onRateMbps,
                  std::uint32_t packetBytes);

    SimTime nextInterval(Rng& rng) override;
    std::unique_ptr<TrafficPattern> clone() const override;
    const char* name() const override;

private:
    SimTime onSec_;
    SimTime offSec_;
    SimTime pos_ = 0.0;
    SimTime gap_;
};

}  // namespace netmind