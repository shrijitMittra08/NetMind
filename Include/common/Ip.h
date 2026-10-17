#pragma once
#include <cstdint>
#include <string>
namespace netmind {
bool tryParseIpv4(const std::string& text, std::uint32_t& out);
std::uint32_t parseIpv4(const std::string& text);
std::string formatIpv4(std::uint32_t addr);
bool isValidIpv4(const std::string& text);
}  // namespace netmind