#pragma once

#include "common/Types.h"
#include "model/Packet.h"

namespace netmind {

class Rng;
class Graph;

class SimContext {
public:
    virtual ~SimContext() = default;

    virtual SimTime now() const = 0;

    virtual void transmit(Packet pkt, NodeId from, NodeId to) = 0;

    virtual void deliver(const Packet& pkt, NodeId at) = 0;

    virtual void drop(const Packet& pkt, NodeId at, DropReason why) = 0;

    virtual Rng& rng() = 0;

    virtual const Graph& graph() const = 0;
};

} 