#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace netmind {

// Returns true only for valid dotted-decimal IPv4 addresses.
[[nodiscard]] inline bool isValidIpv4(std::string_view ip) {
    unsigned int octet = 0;
    unsigned int digits = 0;
    unsigned int octets = 0;

    if (ip.empty()) {
        return false;
    }

    for (const char ch : ip) {
        if (ch >= '0' && ch <= '9') {
            ++digits;

            if (digits > 3) {
                return false;
            }

            octet = octet * 10U +
                    static_cast<unsigned int>(ch - '0');

            if (octet > 255U) {
                return false;
            }
        } else if (ch == '.') {
            if (digits == 0) {
                return false;
            }

            ++octets;
            if (octets >= 4) {
                return false;
            }

            octet = 0;
            digits = 0;
        } else {
            return false;
        }
    }

    return digits > 0 && octets == 3;
}

// Parses IPv4 into a 32-bit integer in network byte order.
// Example: 192.168.1.10 -> 0xC0A8010A.
[[nodiscard]] inline std::uint32_t parseIpv4(std::string_view ip) {
    if (!isValidIpv4(ip)) {
        throw std::invalid_argument("Invalid IPv4 address: " +
                                    std::string(ip));
    }

    std::uint32_t address = 0;
    std::uint32_t octet = 0;

    for (const char ch : ip) {
        if (ch == '.') {
            address = (address << 8U) | octet;
            octet = 0;
        } else {
            octet = octet * 10U +
                    static_cast<std::uint32_t>(ch - '0');
        }
    }

    address = (address << 8U) | octet;
    return address;
}

// Converts a network-byte-order IPv4 integer to dotted-decimal text.
[[nodiscard]] inline std::string formatIpv4(std::uint32_t address) {
    const auto a = (address >> 24U) & 0xFFU;
    const auto b = (address >> 16U) & 0xFFU;
    const auto c = (address >> 8U) & 0xFFU;
    const auto d = address & 0xFFU;

    return std::to_string(a) + "." +
           std::to_string(b) + "." +
           std::to_string(c) + "." +
           std::to_string(d);
}

// Returns whether two IPv4 addresses share the given network prefix.
// A prefix length of 0 matches every address; 32 requires an exact match.
[[nodiscard]] inline bool sameSubnet(
    std::uint32_t addressA,
    std::uint32_t addressB,
    unsigned int prefixLength
) {
    if (prefixLength > 32U) {
        throw std::invalid_argument(
            "IPv4 prefix length must be between 0 and 32");
    }

    if (prefixLength == 0U) {
        return true;
    }

    const std::uint32_t mask =
        0xFFFFFFFFU << (32U - prefixLength);

    return (addressA & mask) == (addressB & mask);
}

[[nodiscard]] inline bool sameSubnet(
    std::string_view addressA,
    std::string_view addressB,
    unsigned int prefixLength
) {
    return sameSubnet(
        parseIpv4(addressA),
        parseIpv4(addressB),
        prefixLength
    );
}

}