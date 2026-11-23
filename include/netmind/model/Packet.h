#pragma once

#include "netmind/common/Types.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace netmind {

struct Packet {
    static constexpr std::uint32_t DEFAULT_TTL = 64;

    PacketId id{};
    NodeId srcId{};
    NodeId dstId{};
    FlowId flowId{};
    std::uint32_t sizeBytes{};
    PacketType type{PacketType::DATA};
    std::uint32_t ttl{DEFAULT_TTL};
    SimTime timestampSent{};
    SimTime originTime{};
    SimTime timestampDelivered{};
    PacketStatus status{PacketStatus::CREATED};
    std::vector<NodeId> pathTaken;
    DropReason dropReason{DropReason::NO_ROUTE};

    [[nodiscard]] static Packet cloneForRetransmit(const Packet& original, PacketId retransmitId) {
        Packet retransmit = original;
        retransmit.id = retransmitId;
        retransmit.ttl = DEFAULT_TTL;
        retransmit.timestampSent = 0.0;
        retransmit.timestampDelivered = 0.0;
        retransmit.status = PacketStatus::CREATED;
        retransmit.pathTaken.clear();
        retransmit.dropReason = DropReason::NO_ROUTE;
        return retransmit;
    }

    [[nodiscard]] static Packet cloneForReply(const Packet& original, PacketId replyId) {
        Packet reply = cloneForRetransmit(original, replyId);
        std::swap(reply.srcId, reply.dstId);
        reply.type = PacketType::ECHO_REPLY;
        return reply;
    }
};

}
