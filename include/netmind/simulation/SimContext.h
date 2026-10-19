#pragma once

#include "netmind/common/Rng.h"
#include "netmind/common/Types.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Packet.h"

namespace netmind {

    class SimContext {
        public:
        virtual ~SimContext() = default;

        [[nodiscard]] virtual SimTime now() const = 0;
        [[nodiscard]] virtual PacketId allocatePacketId() = 0;

        virtual void transmit(Packet pkt, NodeId from, NodeId to) = 0;

        virtual void deliver(const Packet& pkt, NodeId at) = 0;

        virtual void drop(
            const Packet& pkt,
            NodeId at,
            DropReason why
        ) = 0;

        virtual Rng& rng() = 0;

        [[nodiscard]] virtual const Graph& graph() const = 0;
    };

}