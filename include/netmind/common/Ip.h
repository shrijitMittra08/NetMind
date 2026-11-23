#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace netmind {

[[nodiscard]] inline bool isValidIpv4(std::string_view ip) {
    unsigned int octetValue = 0;
    unsigned int digitsInOctet = 0;
    unsigned int dotsSeen = 0;
    for (const char ch : ip) {
        if (ch >= '0' && ch <= '9') {
            ++digitsInOctet;
            octetValue = octetValue * 10U + static_cast<unsigned int>(ch - '0');
            if (digitsInOctet > 3 || octetValue > 255U) {
                return false;
            }
        } else if (ch == '.') {
            ++dotsSeen;
            if (digitsInOctet == 0 || dotsSeen >= 4) {
                return false;
            }
            octetValue = 0;
            digitsInOctet = 0;
        } else {
            return false;
        }
    }
    return digitsInOctet > 0 && dotsSeen == 3;
}

[[nodiscard]] inline std::uint32_t parseIpv4(std::string_view ip) {
    if (!isValidIpv4(ip)) {
        throw std::invalid_argument("Invalid IPv4 address: " + std::string(ip));
    }
    std::uint32_t address = 0;
    std::uint32_t octetValue = 0;
    for (const char ch : ip) {
        if (ch == '.') {
            address = (address << 8U) | octetValue;
            octetValue = 0;
        } else {
            octetValue = octetValue * 10U + static_cast<std::uint32_t>(ch - '0');
        }
    }
    return (address << 8U) | octetValue;
}

[[nodiscard]] inline std::string formatIpv4(std::uint32_t address) {
    return std::to_string((address >> 24U) & 0xFFU) + "." + std::to_string((address >> 16U) & 0xFFU) +
           "." + std::to_string((address >> 8U) & 0xFFU) + "." + std::to_string(address & 0xFFU);
}

[[nodiscard]] inline bool sameSubnet(std::uint32_t addressA, std::uint32_t addressB,
                                     unsigned int prefixLength) {
    if (prefixLength > 32U) {
        throw std::invalid_argument("IPv4 prefix length must be between 0 and 32");
    }
    if (prefixLength == 0U) {
        return true;
    }
    const std::uint32_t mask = 0xFFFFFFFFU << (32U - prefixLength);
    return (addressA & mask) == (addressB & mask);
}

[[nodiscard]] inline bool sameSubnet(std::string_view addressA, std::string_view addressB,
                                     unsigned int prefixLength) {
    return sameSubnet(parseIpv4(addressA), parseIpv4(addressB), prefixLength);
}

}
