#include "common/Ip.h"
#include <sstream>
#include <stdexcept>
namespace netmind {
bool tryParseIpv4(const std::string& text, std::uint32_t& out) {
    std::stringstream ss(text);
    int parts[4];
    char dot1, dot2, dot3;
    if (!(ss >> parts[0] >> dot1 >>
          parts[1] >> dot2 >>
          parts[2] >> dot3 >>
          parts[3])) {
        return false;
    }
    if (dot1 != '.' || dot2 != '.' || dot3 != '.') {
        return false;
    }
    std::string extra;
    if (ss >> extra) {
        return false;
    }
    for (int part : parts) {
        if (part < 0 || part > 255) {
            return false;
        }
    }
    out = (static_cast<std::uint32_t>(parts[0]) << 24) |
          (static_cast<std::uint32_t>(parts[1]) << 16) |
          (static_cast<std::uint32_t>(parts[2]) << 8) |
          static_cast<std::uint32_t>(parts[3]);

    return true;
}
std::uint32_t parseIpv4(const std::string& text) {
    std::uint32_t out = 0;
    if (!tryParseIpv4(text, out)) {
        throw std::invalid_argument("Invalid IPv4 address: " + text);
    }
    return out;
}
std::string formatIpv4(std::uint32_t addr) {
    return std::to_string((addr >> 24) & 0xFF) + "." +
           std::to_string((addr >> 16) & 0xFF) + "." +
           std::to_string((addr >> 8) & 0xFF) + "." +
           std::to_string(addr & 0xFF);
}
bool isValidIpv4(const std::string& text) {
    std::uint32_t out = 0;
    return tryParseIpv4(text, out);
}
}