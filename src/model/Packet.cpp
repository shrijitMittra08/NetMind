#include "model/Packet.h"
#include <stdexcept>
#include <utility>
namespace netmind {
Packet::Packet(PacketId id,
               NodeId src,
               NodeId dst,
               FlowId flow,
               std::uint32_t bytes,
               PacketType type,
               std::uint32_t ttl,
               SimTime now)
    : id(id),
      srcId(src),
      dstId(dst),
      flowId(flow),
      sizeBytes(bytes),
      type(type),
      ttl(ttl),
      timestampSent(now),
      originTime(now),
      timestampDelivered(-1.0),
      status(PacketStatus::CREATED),
      dropReason(DropReason::NO_ROUTE) {
    if (bytes == 0) {
        throw std::invalid_argument("Packet size must be greater than 0");
    }
}
Packet Packet::cloneForReply(const Packet& orig, PacketId newId, SimTime now) {
    Packet reply(newId,
                 orig.dstId,
                 orig.srcId,
                 orig.flowId,
                 orig.sizeBytes,
                 PacketType::ECHO_REPLY,
                 64,
                 now);
    reply.originTime = orig.originTime;
    return reply;
}
Packet Packet::cloneForRetransmit(const Packet& orig, PacketId newId, SimTime now) {
    Packet packet(newId,
                  orig.srcId,
                  orig.dstId,
                  orig.flowId,
                  orig.sizeBytes,
                  orig.type,
                  orig.ttl,
                  now);
    packet.originTime = now;
    return packet;
}
void Packet::markDelivered(SimTime t) {
    status = PacketStatus::DELIVERED;
    timestampDelivered = t;
}
void Packet::markDropped(DropReason reason) {
    status = PacketStatus::DROPPED;
    dropReason = reason;
}
SimTime Packet::latencySec() const {
    if (status != PacketStatus::DELIVERED) {
        throw std::logic_error("Packet has not been delivered");
    }
    return timestampDelivered - timestampSent;
}
SimTime Packet::rttSec() const {
    if (status != PacketStatus::DELIVERED) {
        throw std::logic_error("Packet has not been delivered");
    }
    return timestampDelivered - originTime;
}
std::size_t Packet::hopCount() const {
    return pathTaken.empty() ? 0 : pathTaken.size() - 1;
}
std::ostream& operator<<(std::ostream& os, const Packet& pkt) {
    os << "pkt " << pkt.id
       << " " << pkt.srcId << "->" << pkt.dstId
       << " flow " << pkt.flowId
       << " " << pkt.sizeBytes << "B";
    switch (pkt.type) {
        case PacketType::DATA:
            os << " DATA";
            break;
        case PacketType::ECHO_REQUEST:
            os << " ECHO_REQUEST";
            break;
        case PacketType::ECHO_REPLY:
            os << " ECHO_REPLY";
            break;
    }
    os << " ttl " << pkt.ttl;
    return os;
}
}