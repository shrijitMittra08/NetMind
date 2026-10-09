#pragma once

#include "netmind/Types.h"

namespace netmind {

    class SimObserver {

    public:
        virtual ~SimObserver() = default;
        virtual void onSent(SimTime, const Packet&) {}
        virtual void onEnqueue(
            SimTime,
            const Packet&,
            const ChannelId&,
            size_t qlen
        ) {}
        virtual void onTxStart(
            SimTime,
            const Packet&,
            const ChannelId&,
            SimTime eta
        ) {}
        virtual void onArrive(
            SimTime,
            const Packet&,
            NodeId,
            NodeId from
        ) {}
        virtual void onForward(
            SimTime,
            const Packet&,
            NodeId at,
            NodeId next
        ) {}
        virtual void onDelivered(
            SimTime,
            const Packet&
        ) {}
        virtual void onDropped(
            SimTime,
            const Packet&,
            const std::string& where,
            DropReason
        ) {}
        virtual void onNetworkEvent(
            SimTime,
            const std::string& type,
            const std::string& target,
            const std::string& detail
        ) {}
    };

}