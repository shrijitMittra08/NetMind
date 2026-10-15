#pragma once

#include "netmind/common/Types.h"
#include "netmind/model/NetworkEntity.h"
#include "netmind/model/Packet.h"

#include <string>

namespace netmind {

    class SimContext;

    class Node : public NetworkEntity {
        public:
        Node(NodeId id, std::string name, std::string ip, NodeType type);
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
        ~Node() override = default;

        [[nodiscard]] NodeId getId() const override;
        [[nodiscard]] const std::string& name() const;
        [[nodiscard]] const std::string& ip() const;
        [[nodiscard]] NodeType type() const;
        [[nodiscard]] bool isUp() const;

        void setUp(bool up);

        virtual void receivePacket(
            Packet pkt, LinkId inLink, SimContext& ctx) = 0;
        
        private:
        NodeId id_;
        std::string name_;
        std::string ip_;
        NodeType type_;
        bool up_{true};
    };

}