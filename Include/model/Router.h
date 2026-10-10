#pragma once
#include "model/ForwardingTable.h"
#include "model/Node.h"
#include "simulation/SimContext.h"
namespace netmind {

class Router : public Node {
public:
    Router(NodeId id, std::string name, std::string ip);

    ForwardingTable& forwardingTable();
    const ForwardingTable& forwardingTable() const;

    void receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) override;

private:
    ForwardingTable forwardingTable_;
};

}