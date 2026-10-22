#pragma once

#include "netmind/model/Node.h"

#include <unordered_set>

namespace netmind {

class Switch : public Node {
public:
    Switch(NodeId id, std::string name, std::string ip);

    void receivePacket(
        Packet pkt,
        LinkId inLink,
        SimContext& ctx
    ) override;

private:
    std::unordered_set<PacketId> seenPacketIds_;
};

}