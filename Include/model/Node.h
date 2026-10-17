#pragma once

#include <string>

#include "model/NetworkEntity.h"

namespace netmind {

class Packet;
class SimContext;

class Node : public NetworkEntity {
public:
    Node(NodeId id, std::string name, std::string ip, NodeType type);
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
  
    NodeId getId() const override;

    const std::string& name() const;
    const std::string& ip() const;

    NodeType type() const;

    bool isUp() const;
    void setUp(bool up);
    virtual std::string describe() const;
    virtual void receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) = 0;

private:
    NodeId id_;
    std::string name_;
    std::string ip_;
    NodeType type_;
    bool up_ = true;
};

} 