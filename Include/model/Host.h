#pragma once

#include "model/Node.h"

namespace netmind {

class Host : public Node {
public:
    Host(NodeId id, std::string name, std::string ip, NodeId gatewayId);

    NodeId gatewayId() const;

    void originate(Packet pkt, SimContext& ctx);

    void receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) override;

private:
    NodeId gatewayId_;
};

}  