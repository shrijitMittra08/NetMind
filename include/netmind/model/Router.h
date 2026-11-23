#pragma once

#include "netmind/model/ForwardingTable.h"
#include "netmind/model/Node.h"

#include <string>

namespace netmind {

class Router : public Node {
  public:
    Router(NodeId id, std::string name, std::string ip);

    [[nodiscard]] ForwardingTable& forwardingTable() { return forwardingTable_; }
    [[nodiscard]] const ForwardingTable& forwardingTable() const { return forwardingTable_; }
    void receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) override;

  private:
    ForwardingTable forwardingTable_;
};

}
