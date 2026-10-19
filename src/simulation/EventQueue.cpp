#include "netmind/simulation/EventQueue.h"

#include "netmind/common/Errors.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace netmind {

EventQueue::EventQueue() = default;

void EventQueue::schedule(
    SimTime time,
    std::shared_ptr<NetworkEvent> event
) {
    if (!std::isfinite(time) || time < 0.0) {
        throw ConfigError(
            "Scheduled event time must be finite and non-negative");
    }

    if (event == nullptr) {
        throw ConfigError(
            "Cannot schedule a null network event");
    }

    events_.push(ScheduledEvent{
        time,
        nextSequence_++,
        std::move(event)
    });
}

bool EventQueue::empty() const {
    return events_.empty();
}

std::size_t EventQueue::size() const {
    return events_.size();
}

std::optional<SimTime> EventQueue::nextTime() const {
    if (events_.empty()) {
        return std::nullopt;
    }

    return events_.top().time;
}

ScheduledEvent EventQueue::pop() {
    if (events_.empty()) {
        throw std::logic_error(
            "Cannot pop an event from an empty event queue");
    }

    ScheduledEvent next = events_.top();
    events_.pop();
    return next;
}

bool EventQueue::LaterEvent::operator()(
    const ScheduledEvent& lhs,
    const ScheduledEvent& rhs
) const {
    if (lhs.time != rhs.time) {
        return lhs.time > rhs.time;
    }

    return lhs.sequence > rhs.sequence;
}

}