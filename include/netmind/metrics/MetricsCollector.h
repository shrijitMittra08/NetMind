#pragma once

#include "netmind/SimObserver.h"

namespace netmind {

class MetricsCollector : public SimObserver {
public:
    MetricsCollector() = default;
    ~MetricsCollector() override = default;

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

} // namespace netmind