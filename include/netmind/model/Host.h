#pragma once

#include <optional>

#include "netmind/model/Node.h"

namespace netmind {

    class Host : public Node {
        public:

        void setGatewayId(NodeId gatewayId);
        [[nodiscard]] std::optional<NodeId> gatewayId() const;
        void originate(Packet pkt, SimContext& ctx);

        Host(NodeId id, std::string name, std::string ip);

        void receivePacket(
            Packet pkt, LinkId inLink, SimContext& ctx
        ) override;

        private:
        std::optional<NodeId> gatewayId_;
    };

}