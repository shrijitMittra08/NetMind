#pragma once

#include "netmind/common/Types.h"
#include "netmind/simulation/NetworkEvent.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

namespace netmind {

struct ScheduledEvent {
    SimTime time{};
    std::uint64_t sequence{};
    std::shared_ptr<NetworkEvent> event;
};

class EventQueue {
public:
    EventQueue();

    void schedule(
        SimTime time,
        std::shared_ptr<NetworkEvent> event
    );

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::optional<SimTime> nextTime() const;

    ScheduledEvent pop();

private:
    struct LaterEvent {
        bool operator()(
            const ScheduledEvent& lhs,
            const ScheduledEvent& rhs
        ) const;
    };

    std::priority_queue<
        ScheduledEvent,
        std::vector<ScheduledEvent>,
        LaterEvent
    > events_;

    std::uint64_t nextSequence_{0};
};

}