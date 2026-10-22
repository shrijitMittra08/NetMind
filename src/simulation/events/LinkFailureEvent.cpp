#include "netmind/simulation/events/LinkFailureEvent.h"

#include "netmind/simulation/SimulationEngine.h"

#include <string>

namespace netmind {

LinkFailureEvent::LinkFailureEvent(LinkId linkId)
    : linkId_(linkId) {}

void LinkFailureEvent::apply(SimulationEngine& engine) {
    engine.setLinkState(linkId_, LinkState::DOWN);
}

std::string LinkFailureEvent::describe() const {
    return "LINK_FAILURE link=" + std::to_string(linkId_);
}

}