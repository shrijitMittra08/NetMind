#include "model/Node.h"
#include <stdexcept>
#include <utility>
#include "common/Ip.h"
namespace netmind {
Node::Node(NodeId id, std::string name, std::string ip, NodeType type)
    : id_(id), name_(std::move(name)), ip_(std::move(ip)), type_(type) {
    if (name_.empty()) {
        throw std::invalid_argument("Node name cannot be empty");
    }
    if (name_.find(' ') != std::string::npos) {
        throw std::invalid_argument("Node name cannot contain spaces");
    }
    if (type_ != NodeType::SWITCH && !isValidIpv4(ip_)) {
        throw std::invalid_argument("Invalid IPv4 address");
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
std::string Node::describe() const {
    return name_ + " (" + ip_ + ")";
}
}