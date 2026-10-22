#include "netmind/model/Node.h"
#include "netmind/common/Errors.h"
#include "netmind/common/Ip.h"

#include <utility>

namespace netmind {

    Node::Node(NodeId id, std::string name, std::string ip, NodeType type) :
    id_(id), name_(std::move(name)), ip_(std::move(ip)), type_(type)
    {
        if (name_.empty()) {
            throw ConfigError("Node name must not be empty");
        }
        if (!isValidIpv4(ip_)) {
            throw ConfigError("Node IP must be a valid IPv4");
        }
    }

    NodeId Node::getId() const {
        return id_;
    }

    const std::string& Node::name() const {
        return name_;
    }

    const std::string& Node::ip() const {
        return ip_;
    }

    NodeType Node::type() const {
        return type_;
    }

    bool Node::isUp() const {
        return up_;
    }

    void Node::setUp(bool up) {
        up_ = up;
    }

}