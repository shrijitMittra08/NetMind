#pragma once
#include "netmind/SimObserver.h"
#include <string>

namespace netmind {

    class TraceLogger : public SimObserver {

    public:
        explicit TraceLogger(const std::string& outputPath);
        ~TraceLogger() override;

        void onSent(SimTime, const Packet&) override;
        void onEnqueue(
            SimTime,
            const Packet&,
            const ChannelId&,
            size_t qlen) override;

        void onTxStart(
            SimTime,
            const Packet&,
            const ChannelId&,
            SimTime eta) override;

        void onArrive(
            SimTime,
            const Packet&,
            NodeId,
            NodeId from) override;

        void onForward(
            SimTime,
            const Packet&,
            NodeId at,
            NodeId next) override;

        void onDelivered(SimTime, const Packet&) override;

        void onDropped(
            SimTime,
            const Packet&,
            const std::string& where,
            DropReason) override;

        void onNetworkEvent(
            SimTime,
            const std::string& type,
            const std::string& target,
            const std::string& detail) override;

    };

}