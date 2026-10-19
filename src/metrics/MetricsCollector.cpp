#include "netmind/metrics/MetricsCollector.h"

#include "netmind/model/Link.h"
#include "netmind/model/Node.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace netmind {
namespace {

std::uint64_t channelKey(const ChannelId& channel) {
    return (static_cast<std::uint64_t>(channel.linkId) << 1) |
        static_cast<std::uint64_t>(channel.dir);
}

double percentile95(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1, rank) - 1];
}

}

MetricsCollector::MetricsCollector(const Graph& graph, SimTime windowSize)
    : graph_(graph),
      windowSize_(windowSize) {
    if (!std::isfinite(windowSize_) || windowSize_ <= 0.0) {
        throw std::invalid_argument(
            "Metrics window size must be finite and positive");
    }
}

std::size_t MetricsCollector::windowIndex(SimTime time) const {
    if (time <= 0.0) {
        return 0;
    }

    return static_cast<std::size_t>(std::floor(time / windowSize_));
}

void MetricsCollector::ensureWindowsThrough(SimTime time) {
    const auto last = windowIndex(time);
    for (std::size_t i = 0; i <= last; ++i) {
        windowStates_.try_emplace(i);
    }
}

MetricsCollector::ChannelState& MetricsCollector::channelState(
    const ChannelId& channel) {
    auto& state = channelStates_[channelKey(channel)];
    state.id = channel;
    return state;
}

void MetricsCollector::updateQueue(
    SimTime time,
    const ChannelId& channel,
    std::size_t queueLength) {
    auto& state = channelState(channel);

    if (time >= state.lastQueueChange) {
        state.queueArea +=
            static_cast<double>(state.queueLength) *
            (time - state.lastQueueChange);
        state.lastQueueChange = time;
    }

    state.queueLength = queueLength;
    state.maxQueue = std::max(state.maxQueue, queueLength);

    ensureWindowsThrough(time);
    auto& window = windowStates_[windowIndex(time)];
    window.maxQueue = std::max(window.maxQueue, queueLength);
}

void MetricsCollector::onSent(SimTime time, const Packet& packet) {
    auto& record = packets_[packet.id];
    record.flowId = packet.flowId;

    if (record.sent) {
        return;
    }
    record.sent = true;

    auto& flow = flowStates_[packet.flowId];
    flow.metrics.flowId = packet.flowId;
    flow.metrics.srcId = packet.srcId;
    flow.metrics.dstId = packet.dstId;
    ++flow.metrics.sent;

    ensureWindowsThrough(time);
    ++windowStates_[windowIndex(time)].sent;

    const Node* source = graph_.getNode(packet.srcId);
    if (source != nullptr) {
        const auto links = graph_.getNeighbours(packet.srcId);
        for (const Link* link : links) {
            if (link == nullptr) {
                continue;
            }

            const ChannelId channel{
                link->getId(),
                packet.srcId == link->a()
                    ? Direction::AtoB
                    : Direction::BtoA};
            record.channelKey = channelKey(channel);
            break;
        }
    }
}

void MetricsCollector::onEnqueue(
    SimTime time,
    const Packet& packet,
    const ChannelId& channel,
    std::size_t qlen) {
    packets_[packet.id].channelKey = channelKey(channel);
    updateQueue(time, channel, qlen);
}

void MetricsCollector::onQueueChanged(
    SimTime time,
    const ChannelId& channel,
    std::size_t qlen) {
    updateQueue(time, channel, qlen);
}

void MetricsCollector::onTxStart(
    SimTime time,
    const Packet& packet,
    const ChannelId& channel,
    SimTime) {
    auto& state = channelState(channel);
    state.bitsTransmitted +=
        static_cast<double>(packet.sizeBytes) * 8.0;
    ++state.txPackets;
    packets_[packet.id].channelKey = channelKey(channel);

    ensureWindowsThrough(time);
    windowStates_[windowIndex(time)]
        .txBitsByChannel[channelKey(channel)] +=
        static_cast<double>(packet.sizeBytes) * 8.0;
}

void MetricsCollector::onArrive(
    SimTime,
    const Packet&,
    NodeId,
    NodeId) {
}

void MetricsCollector::onForward(
    SimTime,
    const Packet& packet,
    NodeId at,
    NodeId next) {
    const Link* link = graph_.linkBetween(at, next);
    if (link != nullptr) {
        const ChannelId channel{
            link->getId(),
            at == link->a() ? Direction::AtoB : Direction::BtoA};
        packets_[packet.id].channelKey = channelKey(channel);
    }
    packets_[packet.id].flowId = packet.flowId;
}

void MetricsCollector::onDelivered(SimTime time, const Packet& packet) {
    auto& record = packets_[packet.id];
    if (record.terminal) {
        return;
    }
    record.terminal = true;
    record.flowId = packet.flowId;

    auto& flow = flowStates_[packet.flowId];
    flow.metrics.flowId = packet.flowId;
    flow.metrics.srcId = packet.srcId;
    flow.metrics.dstId = packet.dstId;
    ++flow.metrics.delivered;

    const double latencyMs =
        std::max(0.0, time - packet.timestampSent) * 1000.0;

    if (packet.type == PacketType::ECHO_REPLY) {
        const double rttMs =
            std::max(0.0, time - packet.originTime) * 1000.0;
        flow.rttSumMs += rttMs;
        ++flow.metrics.rttSamples;
    }

    flow.latenciesMs.push_back(latencyMs);
    flow.latencySumMs += latencyMs;
    flow.hopSum += packet.pathTaken.empty()
        ? 0.0
        : static_cast<double>(packet.pathTaken.size() - 1);

    if (flow.metrics.delivered == 1) {
        flow.metrics.minLatencyMs = latencyMs;
        flow.metrics.maxLatencyMs = latencyMs;
    } else {
        flow.metrics.minLatencyMs =
            std::min(flow.metrics.minLatencyMs, latencyMs);
        flow.metrics.maxLatencyMs =
            std::max(flow.metrics.maxLatencyMs, latencyMs);
    }

    if (flow.hasLastLatency) {
        flow.latencyDeltaSum += std::abs(latencyMs - flow.lastLatencyMs);
        ++flow.latencyDeltaCount;
    }
    flow.lastLatencyMs = latencyMs;
    flow.hasLastLatency = true;

    flow.bytesDelivered += packet.sizeBytes;
    if (packet.type == PacketType::DATA) {
        flow.dataBytesDelivered += packet.sizeBytes;
    }

    ensureWindowsThrough(time);
    auto& window = windowStates_[windowIndex(time)];
    ++window.delivered;
    window.deliveredBytes += packet.sizeBytes;
    window.latenciesMs.push_back(latencyMs);

    if (window.hasLastLatency) {
        window.latencyDeltaSum +=
            std::abs(latencyMs - window.lastLatencyMs);
        ++window.latencyDeltaCount;
    }
    window.lastLatencyMs = latencyMs;
    window.hasLastLatency = true;

    const auto interrupted = interruptedFlows_.find(packet.flowId);
    if (interrupted != interruptedFlows_.end()) {
        network_.recoveryTimeS = std::max(
            network_.recoveryTimeS,
            std::max(0.0, time - interrupted->second));
        interruptedFlows_.erase(interrupted);
    }
}

void MetricsCollector::onDropped(
    SimTime time,
    const Packet& packet,
    const std::string&,
    DropReason reason) {
    auto& record = packets_[packet.id];
    if (record.terminal) {
        return;
    }
    record.terminal = true;
    record.flowId = packet.flowId;

    auto& flow = flowStates_[packet.flowId];
    flow.metrics.flowId = packet.flowId;
    flow.metrics.srcId = packet.srcId;
    flow.metrics.dstId = packet.dstId;
    ++flow.metrics.dropped;

    ++network_.totalDrops;

    ensureWindowsThrough(time);
    ++windowStates_[windowIndex(time)].dropped;

    if (reason == DropReason::LINK_DOWN ||
        reason == DropReason::NODE_DOWN) {
        interruptedFlows_.try_emplace(packet.flowId, time);
    }

    const auto state = channelStates_.find(record.channelKey);
    if (state != channelStates_.end()) {
        if (reason == DropReason::QUEUE_FULL) {
            ++state->second.dropsQueue;
        } else if (reason == DropReason::RANDOM_LOSS) {
            ++state->second.dropsLoss;
        } else if (reason == DropReason::LINK_DOWN ||
                   reason == DropReason::NODE_DOWN) {
            ++state->second.dropsDown;
        }
    }
}

void MetricsCollector::onNetworkEvent(
    SimTime time,
    const std::string& type,
    const std::string& target,
    const std::string& detail) {
    ++network_.events;
    ensureWindowsThrough(time);

    if (type == "FLOW_START" || type == "FLOW_STOP") {
        try {
            const auto id = static_cast<FlowId>(std::stoul(target));
            auto& flow = flowStates_[id];
            flow.metrics.flowId = id;

            if (type == "FLOW_START") {
                flow.startTime = time;
                flow.hasStart = true;
            } else {
                flow.stopTime = time;
                flow.hasStop = true;
            }

            const auto srcPos = detail.find("src=");
            const auto dstPos = detail.find(";dst=");
            if (srcPos != std::string::npos &&
                dstPos != std::string::npos) {
                const auto srcText =
                    detail.substr(srcPos + 4, dstPos - srcPos - 4);
                const auto dstText = detail.substr(dstPos + 5);
                flow.metrics.srcId =
                    static_cast<NodeId>(std::stoul(srcText));
                flow.metrics.dstId =
                    static_cast<NodeId>(std::stoul(dstText));
            }
        } catch (const std::exception&) {}
    }

    if (type == "LINK_DOWN") {
        try {
            const auto id = static_cast<LinkId>(std::stoul(target));
            linkDownTimes_[id] = time;

            if (graph_.getLink(id) != nullptr) {
                const ChannelId ab{id, Direction::AtoB};
                const ChannelId ba{id, Direction::BtoA};
                (void)channelState(ab);
                (void)channelState(ba);
            }

            activeFailures_["LINK_DOWN:" + target] = time;
        } catch (const std::exception&) {
            // Ignore malformed link identifiers.
        }
    } else if (type == "LINK_UP") {
        try {
            const auto id = static_cast<LinkId>(std::stoul(target));
            const auto down = linkDownTimes_.find(id);
            if (down != linkDownTimes_.end()) {
                linkDowntimeS_[id] += std::max(0.0, time - down->second);
                linkDownTimes_.erase(down);
            }
            activeFailures_.erase("LINK_DOWN:" + target);
        } catch (const std::exception&) {}
    }

    if (type == "ROUTER_DOWN") {
        activeFailures_["ROUTER_DOWN:" + target] = time;
    } else if (type == "ROUTER_UP") {
        activeFailures_.erase("ROUTER_DOWN:" + target);
    }

    windowStates_[windowIndex(time)].activeFailures =
        activeFailures_.size();
}

std::string MetricsCollector::channelName(const ChannelId& channel) const {
    const Link* link = graph_.getLink(channel.linkId);
    if (link == nullptr) {
        return std::to_string(channel.linkId);
    }

    const bool forward = channel.dir == Direction::AtoB;
    const Node* from = graph_.getNode(forward ? link->a() : link->b());
    const Node* to = graph_.getNode(forward ? link->b() : link->a());

    const std::string fromName =
        from == nullptr ? std::to_string(channel.linkId) : from->name();
    const std::string toName =
        to == nullptr ? std::to_string(channel.linkId) : to->name();

    return fromName + ">" + toName;
}

void MetricsCollector::finalize(SimTime endTime) {
    if (!std::isfinite(endTime) || endTime < 0.0) {
        throw std::invalid_argument(
            "Metrics end time must be finite and non-negative");
    }

    endTime_ = endTime;

    for (auto& entry : channelStates_) {
        auto& state = entry.second;
        if (endTime_ >= state.lastQueueChange) {
            state.queueArea +=
                static_cast<double>(state.queueLength) *
                (endTime_ - state.lastQueueChange);
            state.lastQueueChange = endTime_;
        }
    }

    flows_.clear();
    for (const auto& entry : flowStates_) {
        const auto& state = entry.second;
        auto metrics = state.metrics;

        metrics.unfinished =
            metrics.sent > metrics.delivered + metrics.dropped
                ? metrics.sent - metrics.delivered - metrics.dropped
                : 0;

        const auto completed = metrics.delivered + metrics.dropped;
        metrics.lossPct = completed == 0
            ? 0.0
            : 100.0 * static_cast<double>(metrics.dropped) /
                static_cast<double>(completed);

        if (metrics.delivered > 0) {
            metrics.avgLatencyMs =
                state.latencySumMs /
                static_cast<double>(metrics.delivered);
            metrics.p95LatencyMs = percentile95(state.latenciesMs);
            metrics.avgHops =
                state.hopSum /
                static_cast<double>(metrics.delivered);
        }

        metrics.jitterMs = state.latencyDeltaCount == 0
            ? 0.0
            : state.latencyDeltaSum /
                static_cast<double>(state.latencyDeltaCount);

        metrics.avgRttMs = metrics.rttSamples == 0
            ? 0.0
            : state.rttSumMs /
                static_cast<double>(metrics.rttSamples);

        const SimTime start = state.hasStart ? state.startTime : 0.0;
        const SimTime stop = state.hasStop ? state.stopTime : endTime_;
        const SimTime activeDuration = std::max(0.0, stop - start);

        if (activeDuration > 0.0) {
            metrics.throughputMbps =
                state.bytesDelivered * 8.0 /
                (activeDuration * 1'000'000.0);
            metrics.goodputMbps =
                state.dataBytesDelivered * 8.0 /
                (activeDuration * 1'000'000.0);
        }

        flows_.push_back(metrics);
    }

    channels_.clear();
    for (const auto& entry : channelStates_) {
        const auto& state = entry.second;
        ChannelMetrics metrics;
        metrics.channel = state.id;
        metrics.name = channelName(state.id);
        metrics.txPackets = state.txPackets;
        metrics.maxQueue = state.maxQueue;
        metrics.avgQueue = endTime_ > 0.0
            ? state.queueArea / endTime_
            : 0.0;
        metrics.dropsQueue = state.dropsQueue;
        metrics.dropsLoss = state.dropsLoss;
        metrics.dropsDown = state.dropsDown;
        metrics.downtimeS = linkDowntimeS_[state.id.linkId];
        const auto activeDown = linkDownTimes_.find(state.id.linkId);
        if (activeDown != linkDownTimes_.end()) {
            metrics.downtimeS +=
                std::max(0.0, endTime_ - activeDown->second);
        }

        const Link* link = graph_.getLink(state.id.linkId);
        if (link != nullptr && endTime_ > 0.0) {
            const double capacityBits =
                link->bandwidthMbps() * 1'000'000.0 * endTime_;
            if (capacityBits > 0.0) {
                metrics.utilizationPct =
                    100.0 * state.bitsTransmitted / capacityBits;
            }
        }

        channels_.push_back(metrics);
    }

    windows_.clear();
    ensureWindowsThrough(endTime_);

    for (const auto& entry : windowStates_) {
        const auto& state = entry.second;
        WindowMetrics metrics;
        metrics.start =
            static_cast<SimTime>(entry.first) * windowSize_;
        metrics.sent = state.sent;
        metrics.delivered = state.delivered;
        metrics.dropped = state.dropped;
        metrics.maxQueue = state.maxQueue;
        metrics.activeFailures = state.activeFailures;

        const auto completed = metrics.delivered + metrics.dropped;
        metrics.lossPct = completed == 0
            ? 0.0
            : 100.0 * static_cast<double>(metrics.dropped) /
                static_cast<double>(completed);

        if (!state.latenciesMs.empty()) {
            double total = 0.0;
            for (double latency : state.latenciesMs) {
                total += latency;
            }
            metrics.avgLatencyMs =
                total / static_cast<double>(state.latenciesMs.size());
        }

        metrics.jitterMs = state.latencyDeltaCount == 0
            ? 0.0
            : state.latencyDeltaSum /
                static_cast<double>(state.latencyDeltaCount);

        const SimTime duration = std::min(
            windowSize_,
            std::max(0.0, endTime_ - metrics.start));

        if (duration > 0.0) {
            metrics.throughputMbps =
                state.deliveredBytes * 8.0 /
                (duration * 1'000'000.0);

            for (const auto& tx : state.txBitsByChannel) {
                const auto channel = channelStates_.find(tx.first);
                if (channel == channelStates_.end()) {
                    continue;
                }

                const Link* link =
                    graph_.getLink(channel->second.id.linkId);
                if (link == nullptr) {
                    continue;
                }

                const double capacityBits =
                    link->bandwidthMbps() * 1'000'000.0 * duration;
                if (capacityBits > 0.0) {
                    metrics.maxUtilizationPct = std::max(
                        metrics.maxUtilizationPct,
                        100.0 * tx.second / capacityBits);
                }
            }
        }

        windows_.push_back(metrics);
    }

    std::size_t deliveredTotal = 0;
    std::size_t droppedTotal = 0;
    double latencyTotal = 0.0;

    for (const auto& flow : flows_) {
        deliveredTotal += flow.delivered;
        droppedTotal += flow.dropped;
        latencyTotal +=
            flow.avgLatencyMs * static_cast<double>(flow.delivered);
    }

    const auto completedTotal = deliveredTotal + droppedTotal;
    network_.reliabilityPct = completedTotal == 0
        ? 0.0
        : 100.0 * static_cast<double>(deliveredTotal) /
            static_cast<double>(completedTotal);
    network_.totalDrops = droppedTotal;
    network_.avgLatencyMs = deliveredTotal == 0
        ? 0.0
        : latencyTotal / static_cast<double>(deliveredTotal);

    if (endTime_ > 0.0) {
        double totalDeliveredBytes = 0.0;
        for (const auto& entry : flowStates_) {
            totalDeliveredBytes += entry.second.bytesDelivered;
        }
        network_.avgThroughputMbps =
            totalDeliveredBytes * 8.0 /
            (endTime_ * 1'000'000.0);
    }
}

const std::vector<FlowMetrics>& MetricsCollector::flows() const {
    return flows_;
}

const std::vector<ChannelMetrics>& MetricsCollector::channels() const {
    return channels_;
}

const std::vector<WindowMetrics>& MetricsCollector::windows() const {
    return windows_;
}

const NetworkMetrics& MetricsCollector::network() const {
    return network_;
}

SimTime MetricsCollector::endTime() const {
    return endTime_;
}

}
