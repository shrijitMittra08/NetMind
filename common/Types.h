
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace netmind {

using NodeId = std::uint32_t;
using LinkId = std::uint32_t;
using FlowId = std::uint32_t;
using PacketId = std::uint64_t;

using SimTime = double;

enum class NodeType {
    ROUTER,
    HOST,
    SWITCH
};

enum class LinkState {
    UP,
    DOWN,
    DEGRADED
};

enum class PacketStatus {
    CREATED,
    IN_TRANSIT,
    DELIVERED,
    DROPPED
};

enum class PacketType {
    DATA,
    ECHO_REQUEST,
    ECHO_REPLY
};

enum class DropReason {
    QUEUE_FULL,
    LINK_DOWN,
    NODE_DOWN,
    NO_ROUTE,
    TTL_EXPIRED,
    RANDOM_LOSS
};

enum class Direction {
    AtoB,
    BtoA
};

struct ChannelId {
    LinkId linkId{};
    Direction dir{Direction::AtoB};
};

inline const char* toString(PacketType type) {
    switch (type) {
        case PacketType::DATA:         return "DATA";
        case PacketType::ECHO_REQUEST: return "ECHO_REQUEST";
        case PacketType::ECHO_REPLY:   return "ECHO_REPLY";
    }
    return "UNKNOWN";
}

inline const char* toString(DropReason reason) {
    switch (reason) {
        case DropReason::QUEUE_FULL:  return "queue_full";
        case DropReason::LINK_DOWN:   return "link_down";
        case DropReason::NODE_DOWN:   return "node_down";
        case DropReason::NO_ROUTE:    return "no_route";
        case DropReason::TTL_EXPIRED: return "ttl_expired";
        case DropReason::RANDOM_LOSS: return "random_loss";
    }
    return "unknown";
}

}