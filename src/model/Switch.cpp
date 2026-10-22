#include "netmind/model/Switch.h"

#include "netmind/simulation/SimContext.h"

#include <utility>

namespace netmind {

Switch::Switch(NodeId id, std::string name, std::string ip)
    : Node(id, std::move(name), std::move(ip), NodeType::SWITCH) {}

void Switch::receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) {
    const auto inserted = seenPacketIds_.insert(pkt.id);
    if (!inserted.second) {
        return;
    }

    const auto neighbours = ctx.graph().getActiveNeighbours(getId());

    for (const Link* link : neighbours) {
        if (link->getId() == inLink) {
            continue;
        }

        ctx.transmit(pkt, getId(), link->other(getId()));
    }
}

}