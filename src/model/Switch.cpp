#include "model/Switch.h"
#include <utility>
#include "model/Packet.h"
#include "simulation/SimContext.h"
namespace netmind {
Switch::Switch(NodeId id, std::string name)
    : Node(id, std::move(name), "", NodeType::SWITCH) {}
void Switch::receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) {
    if (!isUp()) {
        ctx.drop(pkt, getId(), DropReason::NODE_DOWN);
        return;
    }
    if (seen_.find(pkt.id) != seen_.end()) {
        ctx.drop(pkt, getId(), DropReason::TTL_EXPIRED);
        return;
    }
    seen_.insert(pkt.id);
    seenOrder_.push_back(pkt.id);
    if (seenOrder_.size() > MAX_SEEN) {
        seen_.erase(seenOrder_.front());
        seenOrder_.pop_front();
    }
    pkt.pathTaken.push_back(getId());
}
}