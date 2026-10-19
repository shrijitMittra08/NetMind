#pragma once

#include "netmind/model/Link.h"

namespace netmind {

class LinkLoadProvider {
public:
    virtual ~LinkLoadProvider() = default;

    [[nodiscard]] virtual double currentQueueDelayMs(
        const Link& link) const = 0;
};

class WeightPolicy {
public:
    virtual ~WeightPolicy() = default;

    [[nodiscard]] virtual double weight(
        const Link& link) const = 0;
};

class HopCountWeight : public WeightPolicy {
public:
    [[nodiscard]] double weight(
        const Link& link) const override;
};

class LatencyWeight : public WeightPolicy {
public:
    [[nodiscard]] double weight(
        const Link& link) const override;
};

class LatencyCongestionWeight : public WeightPolicy {
public:
    explicit LatencyCongestionWeight(
        double alpha,
        const LinkLoadProvider* loadProvider = nullptr);

    [[nodiscard]] double weight(
        const Link& link) const override;

private:
    double alpha_;
    const LinkLoadProvider* loadProvider_;
};

}