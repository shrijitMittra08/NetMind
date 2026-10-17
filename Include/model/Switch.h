#pragma once

#include <deque>
#include <unordered_set>

#include "model/Node.h"

namespace netmind {

class Switch : public Node {
public:
    Switch(NodeId id, std::string name);

    void receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) override;

private:
    std::unordered_set<PacketId> seen_;
    std::deque<PacketId> seenOrder_;

    static constexpr std::size_t MAX_SEEN = 1024;
};

}  // namespace netmind