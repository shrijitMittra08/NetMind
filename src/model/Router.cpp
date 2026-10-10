#include "model/Router.h"
#include <utility>
namespace netmind {
Router::Router(NodeId id, std::string name, std::string ip)
    : Node(id, std::move(name), std::move(ip), NodeType::ROUTER) {}
ForwardingTable& Router::forwardingTable() {
    return forwardingTable_;
}
const ForwardingTable& Router::forwardingTable() const {
    return forwardingTable_;
}
void Router::receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) {
    if (!isUp()) {
        ctx.drop(pkt, getId(), DropReason::NODE_DOWN);
        return;
    }
    pkt.pathTaken.push_back(getId());
    if (pkt.ttl <= 1) {
        ctx.drop(pkt, getId(), DropReason::TTL_EXPIRED);
        return;
    }
    --pkt.ttl;
    if (pkt.dstId == getId()) {
    pkt.markDelivered(ctx.now());
    ctx.deliver(pkt, getId());
    return;
}
    auto entry = forwardingTable_.lookup(pkt.dstId);
    if (!entry.has_value()) {
        ctx.drop(pkt, getId(), DropReason::NO_ROUTE);
        return;
    }
    ctx.transmit(pkt, getId(), entry->nextHop);
}
}  