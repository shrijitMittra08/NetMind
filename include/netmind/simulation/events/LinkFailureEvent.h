#pragma once

#include "netmind/common/Types.h"
#include "netmind/simulation/NetworkEvent.h"

namespace netmind {

class LinkFailureEvent final : public NetworkEvent {
public:
    explicit LinkFailureEvent(LinkId linkId);

    void apply(SimulationEngine& engine) override;
    [[nodiscard]] std::string describe() const override;

private:
    LinkId linkId_;
};

}