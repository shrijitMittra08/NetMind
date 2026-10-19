#pragma once

#include <cstddef>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>

#include "netmind/SimObserver.h"

namespace netmind {

class TraceLogger : public SimObserver {
  public:
    using NodeNameResolver = std::function<std::string(NodeId)>;
    using ChannelNameResolver = std::function<std::string(const ChannelId&)>;

    explicit TraceLogger(const std::string& outputPath);

    TraceLogger(const std::string& outputPath, NodeNameResolver nodeNameResolver,
                ChannelNameResolver channelNameResolver);

    ~TraceLogger() override;

    void flush();

    void onSent(SimTime time, const Packet& packet) override;

    void onEnqueue(SimTime time, const Packet& packet, const ChannelId& channel,
                   std::size_t queueLength) override;

    void onTxStart(SimTime time, const Packet& packet, const ChannelId& channel,
                   SimTime eta) override;

    void onArrive(SimTime time, const Packet& packet, NodeId node, NodeId from) override;

    void onForward(SimTime time, const Packet& packet, NodeId at, NodeId next) override;

    void onDelivered(SimTime time, const Packet& packet) override;

    void onDropped(SimTime time, const Packet& packet, const std::string& where,
                   DropReason reason) override;

    void onNetworkEvent(SimTime time, const std::string& type, const std::string& target,
                        const std::string& detail) override;

  private:
    struct FlowTraceInfo {
        std::string sourceName;
        std::string destinationName;
    };

    [[nodiscard]] std::string nameOf(NodeId id) const;
    [[nodiscard]] std::string channelName(const ChannelId& channel) const;

    std::ofstream output_;
    std::size_t linesSinceFlush_{0};
    NodeNameResolver nodeNameResolver_;
    ChannelNameResolver channelNameResolver_;
    std::unordered_map<std::string, FlowTraceInfo> flowTraceInfo_;
};

}
