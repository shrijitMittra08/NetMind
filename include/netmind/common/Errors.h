#pragma once

#include <stdexcept>
#include <string>

namespace netmind {

class ConfigError : public std::runtime_error {
  public:
    explicit ConfigError(const std::string& message) : std::runtime_error(message) {}
};

class TopologyError : public std::runtime_error {
  public:
    explicit TopologyError(const std::string& message) : std::runtime_error(message) {}
};

}