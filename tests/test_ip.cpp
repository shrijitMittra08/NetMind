#include "netmind/common/Ip.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>

TEST_CASE("IPv4 helpers validate, parse, and format addresses") {
    SECTION("validates dotted-decimal IPv4 addresses") {
        REQUIRE(netmind::isValidIpv4("192.168.1.10"));
        REQUIRE(netmind::isValidIpv4("0.0.0.0"));
        REQUIRE(netmind::isValidIpv4("255.255.255.255"));
        REQUIRE(netmind::isValidIpv4("001.002.003.004"));
    }

    SECTION("rejects malformed or out-of-range addresses") {
        REQUIRE_FALSE(netmind::isValidIpv4(""));
        REQUIRE_FALSE(netmind::isValidIpv4("192.168.1"));
        REQUIRE_FALSE(netmind::isValidIpv4("192.168.1.10.5"));
        REQUIRE_FALSE(netmind::isValidIpv4("256.168.1.10"));
        REQUIRE_FALSE(netmind::isValidIpv4("192..1.10"));
        REQUIRE_FALSE(netmind::isValidIpv4("192.168.-1.10"));
        REQUIRE_FALSE(netmind::isValidIpv4("192.168.1.1000"));
    }

    SECTION("parses IPv4 into network byte order") {
        REQUIRE(netmind::parseIpv4("192.168.1.10") == 0xC0A8010AU);
        REQUIRE(netmind::parseIpv4("0.0.0.0") == 0U);
        REQUIRE(netmind::parseIpv4("255.255.255.255") == 0xFFFFFFFFU);
    }

    SECTION("throws when parsing an invalid address") {
        REQUIRE_THROWS_AS(
            netmind::parseIpv4("192.168.1.256"),
            std::invalid_argument
        );
    }

    SECTION("formats IPv4 integers as dotted-decimal text") {
        REQUIRE(
            netmind::formatIpv4(0xC0A8010AU) ==
            "192.168.1.10"
        );
        REQUIRE(netmind::formatIpv4(0U) == "0.0.0.0");
        REQUIRE(
            netmind::formatIpv4(0xFFFFFFFFU) ==
            "255.255.255.255"
        );
    }

    SECTION("parse and format round-trip") {
        const auto address = netmind::parseIpv4("10.20.30.40");
        REQUIRE(netmind::formatIpv4(address) == "10.20.30.40");
    }
}

TEST_CASE("sameSubnet compares IPv4 network prefixes") {
    SECTION("compares a /24 prefix") {
        REQUIRE(netmind::sameSubnet(
            "192.168.1.10",
            "192.168.1.200",
            24
        ));
        REQUIRE_FALSE(netmind::sameSubnet(
            "192.168.1.10",
            "192.168.2.10",
            24
        ));
    }

    SECTION("compares a /8 prefix") {
        REQUIRE(netmind::sameSubnet(
            "10.1.2.3",
            "10.200.250.1",
            8
        ));
        REQUIRE_FALSE(netmind::sameSubnet(
            "10.1.2.3",
            "11.1.2.3",
            8
        ));
    }

    SECTION("a /0 prefix matches every address") {
        REQUIRE(netmind::sameSubnet(
            "192.168.1.10",
            "10.20.30.40",
            0
        ));
    }

    SECTION("a /32 prefix requires an exact address match") {
        REQUIRE(netmind::sameSubnet(
            "192.168.1.10",
            "192.168.1.10",
            32
        ));
        REQUIRE_FALSE(netmind::sameSubnet(
            "192.168.1.10",
            "192.168.1.11",
            32
        ));
    }

    SECTION("works with already parsed addresses") {
        const std::uint32_t addressA =
            netmind::parseIpv4("172.16.5.10");
        const std::uint32_t addressB =
            netmind::parseIpv4("172.16.5.200");

        REQUIRE(netmind::sameSubnet(addressA, addressB, 24));
    }

    SECTION("rejects prefix lengths above 32") {
        REQUIRE_THROWS_AS(
            netmind::sameSubnet("192.168.1.10", "192.168.1.20", 33),
            std::invalid_argument
        );
    }

    SECTION("string overload rejects invalid addresses") {
        REQUIRE_THROWS_AS(
            netmind::sameSubnet("192.168.1.10", "192.168.1.256", 24),
            std::invalid_argument
        );
    }
}