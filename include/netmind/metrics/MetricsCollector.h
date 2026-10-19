#pragma once

#include "netmind/SimObserver.h"
#include "netmind/graph/Graph.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace netmind {

struct FlowMetrics {
    FlowId flowId{};
    NodeId srcId{};
    NodeId dstId{};
    std::size_t sent{};
    std::size_t delivered{};
    std::size_t dropped{};
    std::size_t unfinished{};
    std::size_t rttSamples{};
    double lossPct{};
    double throughputMbps{};
    double goodputMbps{};
    double avgLatencyMs{};
    double minLatencyMs{};
    double maxLatencyMs{};
    double p95LatencyMs{};
    double jitterMs{};
    double avgHops{};
    double avgRttMs{};
};

struct ChannelMetrics {
    ChannelId channel;
    std::string name;
    double utilizationPct{};
    double avgQueue{};
    std::size_t maxQueue{};
    std::size_t txPackets{};
    std::size_t dropsQueue{};
    std::size_t dropsLoss{};
    std::size_t dropsDown{};
    double downtimeS{};
};

struct WindowMetrics {
    SimTime start{};
    std::size_t sent{};
    std::size_t delivered{};
    std::size_t dropped{};
    double lossPct{};
    double avgLatencyMs{};
    double jitterMs{};
    double throughputMbps{};
    std::size_t maxQueue{};
    double maxUtilizationPct{};
    std::size_t activeFailures{};
};

struct NetworkMetrics {
    double reliabilityPct{};
    double avgThroughputMbps{};
    double avgLatencyMs{};
    std::size_t totalDrops{};
    double recoveryTimeS{};
    std::size_t events{};
};

class MetricsCollector : public SimObserver {
public:
    explicit MetricsCollector(
        const Graph& graph,
        SimTime windowSize = 1.0);

    void onSent(SimTime time, const Packet& packet) override;
    void onEnqueue(
        SimTime time,
        const Packet& packet,
        const ChannelId& channel,
        std::size_t qlen) override;
    void onQueueChanged(
        SimTime time,
        const ChannelId& channel,
        std::size_t qlen) override;
    void onTxStart(
        SimTime time,
        const Packet& packet,
        const ChannelId& channel,
        SimTime eta) override;
    void onArrive(
        SimTime time,
        const Packet& packet,
        NodeId at,
        NodeId from) override;
    void onForward(
        SimTime time,
        const Packet& packet,
        NodeId at,
        NodeId next) override;
    void onDelivered(SimTime time, const Packet& packet) override;
    void onDropped(
        SimTime time,
        const Packet& packet,
        const std::string& where,
        DropReason reason) override;
    void onNetworkEvent(
        SimTime time,
        const std::string& type,
        const std::string& target,
        const std::string& detail) override;

    void finalize(SimTime endTime);

    [[nodiscard]] const std::vector<FlowMetrics>& flows() const;
    [[nodiscard]] const std::vector<ChannelMetrics>& channels() const;
    [[nodiscard]] const std::vector<WindowMetrics>& windows() const;
    [[nodiscard]] const NetworkMetrics& network() const;
    [[nodiscard]] SimTime endTime() const;

private:
    struct PacketRecord {
        FlowId flowId{};
        bool sent{};
        bool terminal{};
        std::uint64_t channelKey{};
    };

    struct FlowState {
        FlowMetrics metrics;
        SimTime startTime{};
        SimTime stopTime{};
        bool hasStart{};
        bool hasStop{};
        double bytesDelivered{};
        double dataBytesDelivered{};
        double latencySumMs{};
        double rttSumMs{};
        double lastLatencyMs{};
        double latencyDeltaSum{};
        std::size_t latencyDeltaCount{};
        bool hasLastLatency{};
        std::vector<double> latenciesMs;
        double hopSum{};
    };

    struct ChannelState {
        ChannelId id;
        std::size_t queueLength{};
        std::size_t maxQueue{};
        SimTime lastQueueChange{};
        double queueArea{};
        double bitsTransmitted{};
        std::size_t txPackets{};
        std::size_t dropsQueue{};
        std::size_t dropsLoss{};
        std::size_t dropsDown{};
    };

    struct WindowState {
        std::size_t sent{};
        std::size_t delivered{};
        std::size_t dropped{};
        std::size_t maxQueue{};
        double deliveredBytes{};
        std::vector<double> latenciesMs;
        double lastLatencyMs{};
        double latencyDeltaSum{};
        std::size_t latencyDeltaCount{};
        bool hasLastLatency{};
        std::map<std::uint64_t, double> txBitsByChannel;
        std::size_t activeFailures{};
    };

    [[nodiscard]] std::size_t windowIndex(SimTime time) const;
    [[nodiscard]] ChannelState& channelState(const ChannelId& channel);
    void updateQueue(
        SimTime time,
        const ChannelId& channel,
        std::size_t queueLength);
    void ensureWindowsThrough(SimTime time);
    [[nodiscard]] std::string channelName(const ChannelId& channel) const;

    const Graph& graph_;
    SimTime windowSize_;
    SimTime endTime_{};

    std::unordered_map<PacketId, PacketRecord> packets_;
    std::map<FlowId, FlowState> flowStates_;
    std::map<std::uint64_t, ChannelState> channelStates_;
    std::map<std::size_t, WindowState> windowStates_;
    std::map<LinkId, SimTime> linkDownTimes_;
    std::map<LinkId, double> linkDowntimeS_;
    std::map<std::string, SimTime> activeFailures_;

    std::map<FlowId, SimTime> interruptedFlows_;

    std::vector<FlowMetrics> flows_;
    std::vector<ChannelMetrics> channels_;
    std::vector<WindowMetrics> windows_;
    NetworkMetrics network_;
};

}