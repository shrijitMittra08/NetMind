#pragma once

#include "netmind/common/Types.h"
#include "netmind/simulation/NetworkEvent.h"

namespace netmind {

class RouterRestoreEvent final : public NetworkEvent {
public:
    explicit RouterRestoreEvent(NodeId routerId);

    void apply(SimulationEngine& engine) override;
    [[nodiscard]] std::string describe() const override;

private:
    NodeId routerId_;
};

}