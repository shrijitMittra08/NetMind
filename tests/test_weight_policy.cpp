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

TEST_CASE("HopCountWeight assigns one to every link") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    netmind::HopCountWeight policy;

    REQUIRE(policy.weight(link) == 1.0);
}

TEST_CASE("LatencyWeight uses the link latency in milliseconds") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    netmind::LatencyWeight policy;

    REQUIRE(policy.weight(link) == 7.5);
}

TEST_CASE("LatencyCongestionWeight adds the weighted queue delay") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    FixedLinkLoad load(4.0);
    netmind::LatencyCongestionWeight policy(2.0, &load);

    REQUIRE(policy.weight(link) == 15.5);
}

TEST_CASE("LatencyCongestionWeight uses zero queue delay without a provider") {
    netmind::Link link(1, 10, 20, 100.0, 7.5, 0.0, 64);
    netmind::LatencyCongestionWeight policy(2.0);

    REQUIRE(policy.weight(link) == 7.5);
}

TEST_CASE("LatencyCongestionWeight validates alpha") {
    REQUIRE_THROWS_AS(netmind::LatencyCongestionWeight(-1.0), netmind::ConfigError);
    REQUIRE_THROWS_AS(netmind::LatencyCongestionWeight(std::numeric_limits<double>::infinity()),
                      netmind::ConfigError);
    REQUIRE_THROWS_AS(netmind::LatencyCongestionWeight(std::numeric_limits<double>::quiet_NaN()),
                      netmind::ConfigError);
}

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