#pragma once

#include "netmind/model/Packet.h"
#include "netmind/common/Types.h"

#include <cstddef>
#include <string>

namespace netmind {

class SimObserver {
public:
    virtual ~SimObserver() = default;
    virtual void onSent(SimTime, const Packet&) {}
    virtual void onEnqueue(SimTime, const Packet&, const ChannelId&, std::size_t) {}
    virtual void onQueueChanged(SimTime, const ChannelId&, std::size_t) {}
    virtual void onTxStart(SimTime, const Packet&, const ChannelId&, SimTime) {}
    virtual void onArrive(SimTime, const Packet&, NodeId, NodeId) {}
    virtual void onForward(SimTime, const Packet&, NodeId, NodeId) {}
    virtual void onDelivered(SimTime, const Packet&) {}
    virtual void onDropped(SimTime, const Packet&, const std::string&, DropReason) {}
    virtual void onNetworkEvent(SimTime, const std::string&, const std::string&, const std::string&) {}
};

}
