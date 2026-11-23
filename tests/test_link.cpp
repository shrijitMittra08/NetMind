#include "netmind/common/Errors.h"
#include "netmind/model/Link.h"
#include "netmind/routing/WeightPolicy.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

class FixedWeight final : public netmind::WeightPolicy {
public:
    explicit FixedWeight(double value)
        : value_(value) {}

    double weight(const netmind::Link&) const override {
        return value_;
    }

private:
    double value_;
};

}             

// Checks that a new link keeps its settings, starts up, and starts at epoch zero
TEST_CASE("Link stores its configuration") {
    netmind::Link link(5, 1, 2, 100.0, 2.5, 0.1, 64);

    REQUIRE(link.getId() == 5);
    REQUIRE(link.a() == 1);
    REQUIRE(link.b() == 2);
    REQUIRE(link.bandwidthMbps() == 100.0);
    REQUIRE(link.latencyMs() == 2.5);
    REQUIRE(link.lossRate() == 0.1);
    REQUIRE(link.queueCapacity() == 64);
    REQUIRE(link.state() == netmind::LinkState::UP);
    REQUIRE(link.epoch() == 0);
}

// Checks that a link connects its two endpoints no matter which one is given first
TEST_CASE("Link connects endpoints in either order") {
    netmind::Link link(5, 1, 2, 100.0, 2.5, 0.0, 64);

    REQUIRE(link.connects(1, 2));
    REQUIRE(link.connects(2, 1));
    REQUIRE_FALSE(link.connects(1, 3));
}

// Checks that a link returns the opposite endpoint and throws for a node it does not touch
TEST_CASE("Link returns the other endpoint") {
    netmind::Link link(5, 1, 2, 100.0, 2.5, 0.0, 64);

    REQUIRE(link.other(1) == 2);
    REQUIRE(link.other(2) == 1);
    REQUIRE_THROWS_AS(link.other(3), netmind::TopologyError);
}

// Checks that creating a link with zero, negative, or infinite bandwidth throws an error
TEST_CASE("Link rejects invalid bandwidth") {
    REQUIRE_THROWS_AS(
        netmind::Link(1, 1, 2, 0.0, 1.0, 0.0, 64),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::Link(1, 1, 2, -10.0, 1.0, 0.0, 64),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::Link(
            1, 1, 2, std::numeric_limits<double>::infinity(), 1.0, 0.0, 64),
        netmind::ConfigError);
}

// Checks that creating a link with negative or NaN latency throws an error
TEST_CASE("Link rejects invalid latency") {
    REQUIRE_THROWS_AS(
        netmind::Link(1, 1, 2, 10.0, -1.0, 0.0, 64),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::Link(
            1, 1, 2, 10.0, std::numeric_limits<double>::quiet_NaN(), 0.0, 64),
        netmind::ConfigError);
}

// Checks that creating a link with a loss rate below 0, above 1, or infinite throws an error
TEST_CASE("Link rejects loss rates outside the range from zero to one") {
    REQUIRE_THROWS_AS(
        netmind::Link(1, 1, 2, 10.0, 1.0, -0.1, 64),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::Link(1, 1, 2, 10.0, 1.0, 1.1, 64),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::Link(
            1, 1, 2, 10.0, 1.0, std::numeric_limits<double>::infinity(), 64),
        netmind::ConfigError);
}

// Checks that taking a link down makes it unusable and bumps its epoch only once per failure
TEST_CASE("Link changes state and advances epoch when it goes down") {
    netmind::Link link(5, 1, 2, 100.0, 2.5, 0.0, 64);

    REQUIRE(link.isUsable());

    link.setState(netmind::LinkState::DOWN);

    REQUIRE_FALSE(link.isUsable());
    REQUIRE(link.epoch() == 1);

    link.setState(netmind::LinkState::DOWN);
    REQUIRE(link.epoch() == 1);

    link.setState(netmind::LinkState::UP);
    REQUIRE(link.isUsable());
    REQUIRE(link.epoch() == 1);

    link.setState(netmind::LinkState::DOWN);
    REQUIRE(link.epoch() == 2);
}

// Checks that bandwidth is reduced by the degrade factor only while the link is degraded
TEST_CASE("Degraded link bandwidth uses its degradation factor") {
    netmind::Link link(5, 1, 2, 100.0, 2.5, 0.0, 64);

    link.setDegradeFactor(0.25);
    REQUIRE(link.effectiveBandwidth() == 100.0);

    link.setState(netmind::LinkState::DEGRADED);
    REQUIRE(link.effectiveBandwidth() == 25.0);
}

// Checks that degrade factors of zero, negative, above one, or NaN are rejected
TEST_CASE("Link rejects invalid degradation factors") {
    netmind::Link link(5, 1, 2, 100.0, 2.5, 0.0, 64);

    REQUIRE_THROWS_AS(link.setDegradeFactor(0.0), netmind::ConfigError);
    REQUIRE_THROWS_AS(link.setDegradeFactor(-0.5), netmind::ConfigError);
    REQUIRE_THROWS_AS(link.setDegradeFactor(1.1), netmind::ConfigError);
    REQUIRE_THROWS_AS(
        link.setDegradeFactor(std::numeric_limits<double>::quiet_NaN()),
        netmind::ConfigError);
}

// Checks that a link asks the given weight policy for its weight
TEST_CASE("Link delegates weight calculation to the selected policy") {
    netmind::Link link(5, 1, 2, 100.0, 2.5, 0.0, 64);
    FixedWeight policy(7.25);

    REQUIRE(link.getWeight(policy) == 7.25);
}