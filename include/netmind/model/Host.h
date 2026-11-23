#pragma once

#include <optional>

#include "netmind/model/Node.h"

namespace netmind {

class Host : public Node {
  public:
    Host(NodeId id, std::string name, std::string ip);

    void setGatewayId(NodeId gatewayId) { gatewayId_ = gatewayId; }
    [[nodiscard]] std::optional<NodeId> gatewayId() const { return gatewayId_; }
    void originate(Packet pkt, SimContext& ctx);
    void receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) override;

  private:
    std::optional<NodeId> gatewayId_;
};

}
