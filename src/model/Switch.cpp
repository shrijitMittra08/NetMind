#include "netmind/model/Switch.h"

#include "netmind/simulation/SimContext.h"

#include <utility>

namespace netmind {

Switch::Switch(NodeId id, std::string name, std::string ip)
    : Node(id, std::move(name), std::move(ip), NodeType::SWITCH) {}

void Switch::receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) {
    const bool firstTimeSeen = seenPacketIds_.insert(pkt.id).second;
    if (!firstTimeSeen) {
        return;
    }
    for (const Link* link : ctx.graph().getActiveNeighbours(getId())) {
        if (link->getId() != inLink) {
            ctx.transmit(pkt, getId(), link->other(getId()));
        }
    }
}

}
