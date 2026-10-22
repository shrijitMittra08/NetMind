#include "netmind/simulation/events/RouterRestoreEvent.h"

#include "netmind/simulation/SimulationEngine.h"

#include <string>

namespace netmind {

RouterRestoreEvent::RouterRestoreEvent(NodeId routerId)
    : routerId_(routerId) {}

void RouterRestoreEvent::apply(SimulationEngine& engine) {
    engine.setRouterState(routerId_, true);
}

std::string RouterRestoreEvent::describe() const {
    return "ROUTER_RESTORE router=" + std::to_string(routerId_);
}

}