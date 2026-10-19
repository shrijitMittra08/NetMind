
#pragma once

#include <string>

namespace netmind {

class SimulationEngine;

class NetworkEvent {
public:
    virtual ~NetworkEvent() = default;

    virtual void apply(SimulationEngine& engine) = 0;
    [[nodiscard]] virtual std::string describe() const = 0;
};

}