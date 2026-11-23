#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "netmind/common/Errors.h"
#include "netmind/model/Link.h"
#include "netmind/routing/WeightPolicy.h"

namespace {

class FixedLinkLoad final : public netmind::LinkLoadProvider {
  public:
    explicit FixedLinkLoad(double queueDelayMs) : queueDelayMs_(queueDelayMs) {}

    double currentQueueDelayMs(const netmind::Link&) const override {
        return queueDelayMs_;
    }

  private:
    double queueDelayMs_;
};

}

// Checks that the hop count policy gives every link a weight of one
TEST_CASE("HopCountWeight assigns one to every link") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    netmind::HopCountWeight policy;

    REQUIRE(policy.weight(link) == 1.0);
}

// Checks that the latency policy uses the link latency as its weight
TEST_CASE("LatencyWeight uses the link latency in milliseconds") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    netmind::LatencyWeight policy;

    REQUIRE(policy.weight(link) == 7.5);
}

// Checks that the congestion policy adds alpha times the queue delay to the latency
TEST_CASE("LatencyCongestionWeight adds the weighted queue delay") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    FixedLinkLoad load(4.0);
    netmind::LatencyCongestionWeight policy(2.0, &load);

    REQUIRE(policy.weight(link) == 15.5);
}

// Checks that the congestion policy uses only latency when no load provider is given
TEST_CASE("LatencyCongestionWeight uses zero queue delay without a provider") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    netmind::LatencyCongestionWeight policy(2.0);

    REQUIRE(policy.weight(link) == 7.5);
}

// Checks that a negative, infinite or NaN alpha value throws a config error
TEST_CASE("LatencyCongestionWeight validates alpha") {
    REQUIRE_THROWS_AS(netmind::LatencyCongestionWeight(-1.0), netmind::ConfigError);
    REQUIRE_THROWS_AS(netmind::LatencyCongestionWeight(std::numeric_limits<double>::infinity()),
                      netmind::ConfigError);
    REQUIRE_THROWS_AS(netmind::LatencyCongestionWeight(std::numeric_limits<double>::quiet_NaN()),
                      netmind::ConfigError);
}

// Checks that a negative or infinite queue delay makes the weight calculation throw
TEST_CASE("LatencyCongestionWeight rejects invalid queue-delay values") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);

    SECTION("negative queue delay") {
        FixedLinkLoad load(-1.0);
        netmind::LatencyCongestionWeight policy(1.0, &load);

        REQUIRE_THROWS_AS(policy.weight(link), netmind::ConfigError);
    }

    SECTION("non-finite queue delay") {
        FixedLinkLoad load(std::numeric_limits<double>::infinity());
        netmind::LatencyCongestionWeight policy(1.0, &load);

        REQUIRE_THROWS_AS(policy.weight(link), netmind::ConfigError);
    }
}