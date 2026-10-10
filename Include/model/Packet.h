#pragma once

#include <cstdint>
#include <ostream>
#include <vector>

#include "common/Types.h"

namespace netmind {

class Packet {
public:
    Packet(PacketId id,
           NodeId src,
           NodeId dst,
           FlowId flow,
           std::uint32_t bytes,
           PacketType type,
           std::uint32_t ttl,
           SimTime now);

    static Packet cloneForReply(const Packet& orig, PacketId newId, SimTime now);

    static Packet cloneForRetransmit(const Packet& orig, PacketId newId, SimTime now);

    void markDelivered(SimTime t);
    void markDropped(DropReason reason);

    SimTime latencySec() const;
    SimTime rttSec() const;
    std::size_t hopCount() const;

    friend std::ostream& operator<<(std::ostream& os, const Packet& pkt);

    PacketId id;
    NodeId srcId;
    NodeId dstId;
    FlowId flowId;
    std::uint32_t sizeBytes;
    PacketType type;
    std::uint32_t ttl;

    SimTime timestampSent;
    SimTime originTime;
    SimTime timestampDelivered;

    PacketStatus status;
    std::vector<NodeId> pathTaken;
    DropReason dropReason;
};

}  