#include "netmind/model/Router.h"

#include "netmind/simulation/SimContext.h"

#include <optional>
#include <utility>

namespace netmind {

Router::Router(NodeId id, std::string name, std::string ip)
    : Node(id, std::move(name), std::move(ip), NodeType::ROUTER) {}

void Router::receivePacket(Packet pkt, LinkId, SimContext& ctx) {
    pkt.pathTaken.push_back(getId());
    if (pkt.ttl <= 1) {
        ctx.drop(pkt, getId(), DropReason::TTL_EXPIRED);
        return;
    }
    --pkt.ttl;
    if (pkt.dstId == getId()) {
        ctx.deliver(pkt, getId());
        return;
    }
    const std::optional<ForwardingTable::Entry> route = forwardingTable_.lookup(pkt.dstId);
    if (!route) {
        ctx.drop(pkt, getId(), DropReason::NO_ROUTE);
        return;
    }
    ctx.transmit(std::move(pkt), getId(), route->nextHop);
}

}
