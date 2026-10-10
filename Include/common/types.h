#pragma once
#include <cstdint>
namespace netmind {
using NodeId = std::uint32_t;
using LinkId = std::uint32_t;
using FlowId = std::uint32_t;
using PacketId = std::uint32_t;
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
}