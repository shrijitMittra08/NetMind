#include "netmind/model/Host.h"

#include "netmind/simulation/SimContext.h"

#include <utility>

namespace netmind {

Host::Host(NodeId id, std::string name, std::string ip)
    : Node(id, std::move(name), std::move(ip), NodeType::HOST) {}

void Host::setGatewayId(NodeId gatewayId) {
    gatewayId_ = gatewayId;
}

std::optional<NodeId> Host::gatewayId() const {
    return gatewayId_;
}

void Host::originate(Packet pkt, SimContext& ctx) {
    pkt.timestampSent = ctx.now();

    if (pkt.type != PacketType::ECHO_REPLY) {
        pkt.originTime = pkt.timestampSent;
    }

    pkt.pathTaken.push_back(getId());

    if (!gatewayId_) {
        ctx.drop(pkt, getId(), DropReason::NO_ROUTE);
        return;
    }

    ctx.transmit(std::move(pkt), getId(), *gatewayId_);
}

void Host::receivePacket(Packet pkt, LinkId, SimContext& ctx) {
    if (pkt.dstId != getId()) {
        ctx.drop(pkt, getId(), DropReason::NO_ROUTE);
        return;
    }

    if (pkt.type != PacketType::ECHO_REQUEST) {
        ctx.deliver(pkt, getId());
        return;
    }

    Packet reply = Packet::cloneForReply(pkt, ctx.allocatePacketId());
    originate(std::move(reply), ctx);
}

}