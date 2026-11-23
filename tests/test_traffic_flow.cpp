#include "netmind/common/Errors.h"
#include "netmind/model/Flow.h"
#include "netmind/model/TrafficPattern.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>

// Checks that a constant 8 Mbps flow of 1000-byte packets sends one packet every 1 ms
TEST_CASE("Constant rate pattern calculates packet intervals in seconds") {
    netmind::ConstantRate pattern(8.0, 1000);
    netmind::Rng rng(42);

    const netmind::SimTime interval = pattern.nextInterval(rng);

    REQUIRE(std::abs(interval - 0.001) < 1e-12);
    REQUIRE(pattern.packetSizeBytes() == 1000);
    REQUIRE(pattern.rateMbps() == 8.0);
}

// Checks that a constant rate pattern with zero rate or zero packet size throws an error
TEST_CASE("Constant rate pattern rejects invalid configuration") {
    REQUIRE_THROWS_AS(
        netmind::ConstantRate(0.0, 1000),
        netmind::ConfigError);
    REQUIRE_THROWS_AS(
        netmind::ConstantRate(8.0, 0),
        netmind::ConfigError);
}

// Checks that a bursty pattern sends packets while ON and then waits for the OFF period
TEST_CASE("Bursty pattern emits packets during ON periods and waits through OFF periods") {
    netmind::BurstyPattern pattern(0.003, 0.002, 8.0, 1000);
    netmind::Rng rng(42);

    REQUIRE(std::abs(pattern.nextInterval(rng) - 0.001) < 1e-12);
    REQUIRE(std::abs(pattern.nextInterval(rng) - 0.001) < 1e-12);
    REQUIRE(std::abs(pattern.nextInterval(rng) - 0.001) < 1e-12);

                                                                       
    REQUIRE(std::abs(pattern.nextInterval(rng) - 0.003) < 1e-12);
    REQUIRE(std::abs(pattern.nextInterval(rng) - 0.001) < 1e-12);
    REQUIRE(pattern.packetSizeBytes() == 1000);
}

// Checks that a bursty pattern with bad ON or OFF times throws a config error
TEST_CASE("Bursty pattern rejects invalid configuration") {
    REQUIRE_THROWS_AS(
        netmind::BurstyPattern(0.0, 0.002, 8.0, 1000),
        netmind::ConfigError);
    REQUIRE_THROWS_AS(
        netmind::BurstyPattern(0.002, -0.001, 8.0, 1000),
        netmind::ConfigError);
    REQUIRE_THROWS_AS(
        netmind::BurstyPattern(0.0005, 0.002, 8.0, 1000),
        netmind::ConfigError);
}

// Checks that a Poisson pattern gives the same valid non-negative gaps for the same seed
TEST_CASE("Poisson pattern returns non-negative intervals reproducibly") {
    netmind::PoissonPattern pattern(5.0, 500);
    netmind::Rng firstRng(42);
    netmind::Rng secondRng(42);

    for (int i = 0; i < 20; ++i) {
        const auto first = pattern.nextInterval(firstRng);
        const auto second = pattern.nextInterval(secondRng);

        REQUIRE(first >= 0.0);
        REQUIRE(std::isfinite(first));
        REQUIRE(first == second);
    }

    REQUIRE(pattern.packetSizeBytes() == 500);
}

// Checks that a Poisson pattern with zero rate or zero packet size throws a config error
TEST_CASE("Poisson pattern rejects invalid configuration") {
    REQUIRE_THROWS_AS(
        netmind::PoissonPattern(0.0, 500),
        netmind::ConfigError);
    REQUIRE_THROWS_AS(
        netmind::PoissonPattern(5.0, 0),
        netmind::ConfigError);
}

// Checks that a flow reports its settings and builds packets with the right fields
TEST_CASE("Flow exposes its configuration and creates packets") {
    auto pattern = std::make_unique<netmind::ConstantRate>(8.0, 1000);
    netmind::Flow flow(7, 1, 3, 2.0, 5.0, std::move(pattern));

    REQUIRE(flow.id() == 7);
    REQUIRE(flow.src() == 1);
    REQUIRE(flow.dst() == 3);
    REQUIRE(flow.startTime() == 2.0);
    REQUIRE(flow.stopTime() == 5.0);
    REQUIRE(flow.pattern().packetSizeBytes() == 1000);

    const netmind::Packet packet = flow.makePacket(2.5, 42);

    REQUIRE(packet.id == 42);
    REQUIRE(packet.srcId == 1);
    REQUIRE(packet.dstId == 3);
    REQUIRE(packet.flowId == 7);
    REQUIRE(packet.sizeBytes == 1000);
    REQUIRE(packet.timestampSent == 2.5);
    REQUIRE(packet.originTime == 2.5);
    REQUIRE(packet.type == netmind::PacketType::DATA);
    REQUIRE(packet.status == netmind::PacketStatus::CREATED);
}

// Checks that a flow with a negative start, stop before start or no pattern throws an error
TEST_CASE("Flow rejects invalid configuration") {
    REQUIRE_THROWS_AS(
        netmind::Flow(
            1,
            1,
            2,
            -1.0,
            5.0,
            std::make_unique<netmind::ConstantRate>(8.0, 1000)),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::Flow(
            1,
            1,
            2,
            5.0,
            2.0,
            std::make_unique<netmind::ConstantRate>(8.0, 1000)),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::Flow(1, 1, 2, 0.0, 5.0, nullptr),
        netmind::ConfigError);
}

// Checks that a flow refuses to make packets before its start time or at its stop time
TEST_CASE("Flow only creates packets within its active interval") {
    netmind::Flow flow(
        7,
        1,
        3,
        2.0,
        5.0,
        std::make_unique<netmind::ConstantRate>(8.0, 1000));

    REQUIRE_THROWS_AS(flow.makePacket(1.9, 1), netmind::ConfigError);
    REQUIRE_THROWS_AS(flow.makePacket(5.0, 2), netmind::ConfigError);
}