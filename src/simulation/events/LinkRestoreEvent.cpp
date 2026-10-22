#include "netmind/simulation/events/LinkRestoreEvent"

#include "netmind/simulation/SimulationEngine.h"

#include <string>

namespace netmind {

LinkRestoreEvent::LinkRestoreEvent(LinkId linkId)
    : linkId_(linkId) {}

void LinkRestoreEvent::apply(SimulationEngine& engine) {
    engine.setLinkState(linkId_, LinkState::UP);
}

std::string LinkRestoreEvent::describe() const {
    return "LINK_RESTORE link=" + std::to_string(linkId_);
}

}