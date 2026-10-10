#include "model/Host.h"
#include <utility>
#include "model/Packet.h"
#include "simulation/SimContext.h"
namespace netmind {
Host::Host(NodeId id, std::string name, std::string ip, NodeId gatewayId)
    : Node(id, std::move(name), std::move(ip), NodeType::HOST),
      gatewayId_(gatewayId) {}
NodeId Host::gatewayId() const {
    return gatewayId_;
}
void Host::originate(Packet pkt, SimContext& ctx) {
    if (!isUp()) {
        ctx.drop(pkt, getId(), DropReason::NODE_DOWN);
        return;
    }
    if (gatewayId_ == static_cast<NodeId>(-1)) {
        ctx.drop(pkt, getId(), DropReason::NO_ROUTE);
        return;
    }
    pkt.status = PacketStatus::IN_TRANSIT;
    pkt.pathTaken.push_back(getId());
    ctx.transmit(pkt, getId(), gatewayId_);
}
void Host::receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) {
    (void)inLink;
    if (!isUp()) {
        ctx.drop(pkt, getId(), DropReason::NODE_DOWN);
        return;
    }
    pkt.pathTaken.push_back(getId());
    if (pkt.dstId != getId()) {
        ctx.drop(pkt, getId(), DropReason::NO_ROUTE);
        return;
    }
    pkt.markDelivered(ctx.now());
    ctx.deliver(pkt, getId());
    if (pkt.type == PacketType::ECHO_REQUEST) {
        Packet reply = Packet::cloneForReply(
            pkt, 0, ctx.now());
        originate(reply, ctx);
    }
}
}