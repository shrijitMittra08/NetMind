#pragma once

#include "netmind/common/Rng.h"
#include "netmind/common/Types.h"

#include <cstdint>

namespace netmind {

class TrafficPattern {
  public:
    virtual ~TrafficPattern() = default;
    [[nodiscard]] virtual SimTime nextInterval(Rng& rng) = 0;
    [[nodiscard]] virtual std::uint32_t packetSizeBytes() const = 0;
};

class ConstantRate final : public TrafficPattern {
  public:
    ConstantRate(double rateMbps, std::uint32_t packetBytes);

    [[nodiscard]] SimTime nextInterval(Rng& rng) override;
    [[nodiscard]] std::uint32_t packetSizeBytes() const override { return packetBytes_; }
    [[nodiscard]] double rateMbps() const { return rateMbps_; }

  private:
    double rateMbps_;
    std::uint32_t packetBytes_;
};

class BurstyPattern final : public TrafficPattern {
  public:
    BurstyPattern(SimTime onDuration, SimTime offDuration, double onRateMbps,
                  std::uint32_t packetBytes);

    [[nodiscard]] SimTime nextInterval(Rng& rng) override;
    [[nodiscard]] std::uint32_t packetSizeBytes() const override { return packetBytes_; }

  private:
    SimTime onDuration_;
    SimTime offDuration_;
    double packetInterval_;
    std::uint32_t packetBytes_;
    SimTime elapsedInOnPeriod_{};
};

class PoissonPattern final : public TrafficPattern {
  public:
    PoissonPattern(double packetsPerSecond, std::uint32_t packetBytes);

    [[nodiscard]] SimTime nextInterval(Rng& rng) override;
    [[nodiscard]] std::uint32_t packetSizeBytes() const override { return packetBytes_; }

  private:
    double packetsPerSecond_;
    std::uint32_t packetBytes_;
};

}
