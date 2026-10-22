#include "netmind/simulation/events/RouterFailureEvent.h"

#include "netmind/simulation/SimulationEngine.h"

#include <string>

namespace netmind {

RouterFailureEvent::RouterFailureEvent(NodeId routerId)
    : routerId_(routerId) {}

void RouterFailureEvent::apply(SimulationEngine& engine) {
    engine.setRouterState(routerId_, false);
}

std::string RouterFailureEvent::describe() const {
    return "ROUTER_FAILURE router=" + std::to_string(routerId_);
}

}