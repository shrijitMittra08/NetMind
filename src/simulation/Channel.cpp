#include "netmind/simulation/Channel.h"

#include <stdexcept>
#include <utility>

namespace netmind {

Channel::Channel(ChannelId id, NodeId from, NodeId to)
    : id_(id),
      from_(from),
      to_(to) {}

ChannelId Channel::id() const {
    return id_;
}

LinkId Channel::linkId() const {
    return id_.linkId;
}

NodeId Channel::from() const {
    return from_;
}

NodeId Channel::to() const {
    return to_;
}

bool Channel::busy() const {
    return busy_;
}

std::size_t Channel::queueSize() const {
    return queue_.size();
}

bool Channel::empty() const {
    return queue_.empty();
}

bool Channel::tryEnqueue(Packet packet, std::size_t capacity) {
    if (queue_.size() >= capacity) {
        return false;
    }

    queue_.push(std::move(packet));
    return true;
}

const Packet& Channel::front() const {
    if (queue_.empty()) {
        throw std::logic_error(
            "Cannot inspect the front of an empty channel");
    }

    return queue_.front();
}

std::uint64_t Channel::beginTransmission() {
    if (busy_) {
        throw std::logic_error(
            "Cannot start a transmission on a busy channel");
    }

    if (queue_.empty()) {
        throw std::logic_error(
            "Cannot start a transmission on an empty channel");
    }

    busy_ = true;
    return ++generation_;
}

std::optional<Packet> Channel::completeTransmission(
    std::uint64_t generation
) {
    if (!busy_ || generation != generation_ || queue_.empty()) {
        return std::nullopt;
    }

    Packet packet = std::move(queue_.front());
    queue_.pop();
    busy_ = false;

    return packet;
}

std::vector<Packet> Channel::flush() {
    std::vector<Packet> packets;
    packets.reserve(queue_.size());

    while (!queue_.empty()) {
        packets.push_back(std::move(queue_.front()));
        queue_.pop();
    }

    busy_ = false;
    ++generation_;
    return packets;
}

}