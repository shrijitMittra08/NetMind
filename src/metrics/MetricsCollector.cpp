#include "netmind/metrics/MetricsCollector.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "netmind/model/Link.h"
#include "netmind/model/Node.h"

namespace netmind {
namespace {

std::uint64_t channelKey(const ChannelId& channel) {
    return (static_cast<std::uint64_t>(channel.linkId) << 1) |
           static_cast<std::uint64_t>(channel.dir);
}

ChannelId channelLeaving(const Link& link, NodeId from) {
    return ChannelId{link.getId(), from == link.a() ? Direction::AtoB : Direction::BtoA};
}

double percentile95(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto rank =
        static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1, rank) - 1];
}

double percentOf(std::size_t part, std::size_t whole) {
    if (whole == 0) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

double averageOrZero(double total, std::size_t count) {
    if (count == 0) {
        return 0.0;
    }
    return total / static_cast<double>(count);
}

double megabitsPerSecond(double bytes, SimTime seconds) {
    return bytes * 8.0 / (seconds * 1'000'000.0);
}

double linkCapacityBits(const Link& link, SimTime seconds) {
    return link.bandwidthMbps() * 1'000'000.0 * seconds;
}

bool isFailureDrop(DropReason reason) {
    return reason == DropReason::LINK_DOWN || reason == DropReason::NODE_DOWN;
}

}

void MetricsCollector::JitterTracker::add(double latencyMs) {
    if (hasLastLatency) {
        latencyDeltaSum += std::abs(latencyMs - lastLatencyMs);
        ++latencyDeltaCount;
    }
    lastLatencyMs = latencyMs;
    hasLastLatency = true;
}

double MetricsCollector::JitterTracker::average() const {
    return averageOrZero(latencyDeltaSum, latencyDeltaCount);
}

void MetricsCollector::ChannelState::addQueueAreaUntil(SimTime time) {
    if (time >= lastQueueChange) {
        queueArea += static_cast<double>(queueLength) * (time - lastQueueChange);
        lastQueueChange = time;
    }
}

MetricsCollector::MetricsCollector(const Graph& graph, SimTime windowSize)
    : graph_(graph), windowSize_(windowSize) {
    if (!std::isfinite(windowSize_) || windowSize_ <= 0.0) {
        throw std::invalid_argument("Metrics window size must be finite and positive");
    }
}

std::size_t MetricsCollector::windowIndex(SimTime time) const {
    if (time <= 0.0) {
        return 0;
    }
    return static_cast<std::size_t>(std::floor(time / windowSize_));
}

void MetricsCollector::ensureWindowsThrough(SimTime time) {
    for (std::size_t i = 0; i <= windowIndex(time); ++i) {
        windowStates_.try_emplace(i);
    }
}

MetricsCollector::WindowState& MetricsCollector::windowAt(SimTime time) {
    ensureWindowsThrough(time);
    return windowStates_[windowIndex(time)];
}

MetricsCollector::ChannelState& MetricsCollector::channelState(const ChannelId& channel) {
    ChannelState& state = channelStates_[channelKey(channel)];
    state.id = channel;
    return state;
}

MetricsCollector::FlowState& MetricsCollector::flowStateFor(const Packet& packet) {
    FlowState& flow = flowStates_[packet.flowId];
    flow.metrics.flowId = packet.flowId;
    flow.metrics.srcId = packet.srcId;
    flow.metrics.dstId = packet.dstId;
    return flow;
}

void MetricsCollector::updateQueue(SimTime time, const ChannelId& channel,
                                   std::size_t queueLength) {
    ChannelState& state = channelState(channel);
    state.addQueueAreaUntil(time);
    state.queueLength = queueLength;
    state.maxQueue = std::max(state.maxQueue, queueLength);
    WindowState& window = windowAt(time);
    window.maxQueue = std::max(window.maxQueue, queueLength);
}

void MetricsCollector::onSent(SimTime time, const Packet& packet) {
    PacketRecord& record = packets_[packet.id];
    record.flowId = packet.flowId;
    if (record.sent) {
        return;
    }
    record.sent = true;
    ++flowStateFor(packet).metrics.sent;
    ++windowAt(time).sent;

    if (graph_.getNode(packet.srcId) == nullptr) {
        return;
    }
    for (const Link* link : graph_.getNeighbours(packet.srcId)) {
        if (link != nullptr) {
            record.channelKey = channelKey(channelLeaving(*link, packet.srcId));
            break;
        }
    }
}

void MetricsCollector::onEnqueue(SimTime time, const Packet& packet, const ChannelId& channel,
                                 std::size_t qlen) {
    packets_[packet.id].channelKey = channelKey(channel);
    updateQueue(time, channel, qlen);
}

void MetricsCollector::onQueueChanged(SimTime time, const ChannelId& channel, std::size_t qlen) {
    updateQueue(time, channel, qlen);
}

void MetricsCollector::onTxStart(SimTime time, const Packet& packet, const ChannelId& channel,
                                 SimTime) {
    const double bits = static_cast<double>(packet.sizeBytes) * 8.0;
    ChannelState& state = channelState(channel);
    state.bitsTransmitted += bits;
    ++state.txPackets;
    packets_[packet.id].channelKey = channelKey(channel);
    windowAt(time).txBitsByChannel[channelKey(channel)] += bits;
}

void MetricsCollector::onArrive(SimTime, const Packet&, NodeId, NodeId) {}

void MetricsCollector::onForward(SimTime, const Packet& packet, NodeId at, NodeId next) {
    PacketRecord& record = packets_[packet.id];
    const Link* link = graph_.linkBetween(at, next);
    if (link != nullptr) {
        record.channelKey = channelKey(channelLeaving(*link, at));
    }
    record.flowId = packet.flowId;
}

void MetricsCollector::onDelivered(SimTime time, const Packet& packet) {
    PacketRecord& record = packets_[packet.id];
    if (record.terminal) {
        return;
    }
    record.terminal = true;
    record.flowId = packet.flowId;

    const double latencyMs = std::max(0.0, time - packet.timestampSent) * 1000.0;
    recordFlowDelivery(flowStateFor(packet), packet, time, latencyMs);

    WindowState& window = windowAt(time);
    ++window.delivered;
    window.deliveredBytes += packet.sizeBytes;
    window.latenciesMs.push_back(latencyMs);
    window.jitter.add(latencyMs);

    if (interruptedFlows_.count(packet.flowId) != 0) {
        const SimTime recovery = std::max(0.0, time - interruptedFlows_.at(packet.flowId));
        network_.recoveryTimeS = std::max(network_.recoveryTimeS, recovery);
        interruptedFlows_.erase(packet.flowId);
    }
}

void MetricsCollector::recordFlowDelivery(FlowState& flow, const Packet& packet, SimTime time,
                                          double latencyMs) {
    FlowMetrics& metrics = flow.metrics;
    ++metrics.delivered;
    if (packet.type == PacketType::ECHO_REPLY) {
        flow.rttSumMs += std::max(0.0, time - packet.originTime) * 1000.0;
        ++metrics.rttSamples;
    }

    flow.latenciesMs.push_back(latencyMs);
    flow.latencySumMs += latencyMs;
    if (!packet.pathTaken.empty()) {
        flow.hopSum += static_cast<double>(packet.pathTaken.size() - 1);
    }

    if (metrics.delivered == 1) {
        metrics.minLatencyMs = latencyMs;
        metrics.maxLatencyMs = latencyMs;
    } else {
        metrics.minLatencyMs = std::min(metrics.minLatencyMs, latencyMs);
        metrics.maxLatencyMs = std::max(metrics.maxLatencyMs, latencyMs);
    }
    flow.jitter.add(latencyMs);

    flow.bytesDelivered += packet.sizeBytes;
    if (packet.type == PacketType::DATA) {
        flow.dataBytesDelivered += packet.sizeBytes;
    }
}

void MetricsCollector::onDropped(SimTime time, const Packet& packet, const std::string&,
                                 DropReason reason) {
    PacketRecord& record = packets_[packet.id];
    if (record.terminal) {
        return;
    }
    record.terminal = true;
    record.flowId = packet.flowId;

    ++flowStateFor(packet).metrics.dropped;
    ++network_.totalDrops;
    ++windowAt(time).dropped;
    if (isFailureDrop(reason)) {
        interruptedFlows_.try_emplace(packet.flowId, time);
    }

    if (channelStates_.count(record.channelKey) == 0) {
        return;
    }
    ChannelState& state = channelStates_.at(record.channelKey);
    if (reason == DropReason::QUEUE_FULL) {
        ++state.dropsQueue;
    } else if (reason == DropReason::RANDOM_LOSS) {
        ++state.dropsLoss;
    } else if (isFailureDrop(reason)) {
        ++state.dropsDown;
    }
}

void MetricsCollector::onNetworkEvent(SimTime time, const std::string& type,
                                      const std::string& target, const std::string& detail) {
    ++network_.events;
    handleFlowEvent(time, type, target, detail);
    handleLinkEvent(time, type, target);
    if (type == "ROUTER_DOWN") {
        activeFailures_["ROUTER_DOWN:" + target] = time;
    } else if (type == "ROUTER_UP") {
        activeFailures_.erase("ROUTER_DOWN:" + target);
    }
    windowAt(time).activeFailures = activeFailures_.size();
}

void MetricsCollector::handleFlowEvent(SimTime time, const std::string& type,
                                       const std::string& target, const std::string& detail) {
    if (type != "FLOW_START" && type != "FLOW_STOP") {
        return;
    }
    try {
        const auto id = static_cast<FlowId>(std::stoul(target));
        FlowState& flow = flowStates_[id];
        flow.metrics.flowId = id;
        if (type == "FLOW_START") {
            flow.startTime = time;
            flow.hasStart = true;
        } else {
            flow.stopTime = time;
            flow.hasStop = true;
        }

        const std::size_t srcPos = detail.find("src=");
        const std::size_t dstPos = detail.find(";dst=");
        if (srcPos != std::string::npos && dstPos != std::string::npos) {
            const std::string srcText = detail.substr(srcPos + 4, dstPos - srcPos - 4);
            flow.metrics.srcId = static_cast<NodeId>(std::stoul(srcText));
            flow.metrics.dstId = static_cast<NodeId>(std::stoul(detail.substr(dstPos + 5)));
        }
    } catch (const std::exception&) {
    }
}

void MetricsCollector::handleLinkEvent(SimTime time, const std::string& type,
                                       const std::string& target) {
    if (type != "LINK_DOWN" && type != "LINK_UP") {
        return;
    }
    try {
        const auto id = static_cast<LinkId>(std::stoul(target));
        if (type == "LINK_DOWN") {
            linkDownTimes_[id] = time;
            if (graph_.getLink(id) != nullptr) {
                (void)channelState(ChannelId{id, Direction::AtoB});
                (void)channelState(ChannelId{id, Direction::BtoA});
            }
            activeFailures_["LINK_DOWN:" + target] = time;
            return;
        }
        if (linkDownTimes_.count(id) != 0) {
            linkDowntimeS_[id] += std::max(0.0, time - linkDownTimes_.at(id));
            linkDownTimes_.erase(id);
        }
        activeFailures_.erase("LINK_DOWN:" + target);
    } catch (const std::exception&) {
    }
}

std::string MetricsCollector::channelName(const ChannelId& channel) const {
    const std::string fallback = std::to_string(channel.linkId);
    const Link* link = graph_.getLink(channel.linkId);
    if (link == nullptr) {
        return fallback;
    }
    const bool forward = channel.dir == Direction::AtoB;
    const Node* from = graph_.getNode(forward ? link->a() : link->b());
    const Node* to = graph_.getNode(forward ? link->b() : link->a());
    const std::string fromName = from != nullptr ? from->name() : fallback;
    const std::string toName = to != nullptr ? to->name() : fallback;
    return fromName + ">" + toName;
}

void MetricsCollector::finalize(SimTime endTime) {
    if (!std::isfinite(endTime) || endTime < 0.0) {
        throw std::invalid_argument("Metrics end time must be finite and non-negative");
    }
    endTime_ = endTime;
    for (auto& entry : channelStates_) {
        entry.second.addQueueAreaUntil(endTime_);
    }

    flows_.clear();
    for (const auto& entry : flowStates_) {
        flows_.push_back(summarizeFlow(entry.second));
    }
    channels_.clear();
    for (const auto& entry : channelStates_) {
        channels_.push_back(summarizeChannel(entry.second));
    }
    windows_.clear();
    ensureWindowsThrough(endTime_);
    for (const auto& entry : windowStates_) {
        windows_.push_back(summarizeWindow(entry.first, entry.second));
    }
    buildNetworkMetrics();
}

FlowMetrics MetricsCollector::summarizeFlow(const FlowState& state) const {
    FlowMetrics metrics = state.metrics;
    const std::size_t completed = metrics.delivered + metrics.dropped;
    metrics.unfinished = metrics.sent > completed ? metrics.sent - completed : 0;
    metrics.lossPct = percentOf(metrics.dropped, completed);
    if (metrics.delivered > 0) {
        metrics.avgLatencyMs = averageOrZero(state.latencySumMs, metrics.delivered);
        metrics.p95LatencyMs = percentile95(state.latenciesMs);
        metrics.avgHops = averageOrZero(state.hopSum, metrics.delivered);
    }
    metrics.jitterMs = state.jitter.average();
    metrics.avgRttMs = averageOrZero(state.rttSumMs, metrics.rttSamples);

    const SimTime start = state.hasStart ? state.startTime : 0.0;
    const SimTime stop = state.hasStop ? state.stopTime : endTime_;
    const SimTime activeDuration = std::max(0.0, stop - start);
    if (activeDuration > 0.0) {
        metrics.throughputMbps = megabitsPerSecond(state.bytesDelivered, activeDuration);
        metrics.goodputMbps = megabitsPerSecond(state.dataBytesDelivered, activeDuration);
    }
    return metrics;
}

ChannelMetrics MetricsCollector::summarizeChannel(const ChannelState& state) const {
    ChannelMetrics metrics;
    metrics.channel = state.id;
    metrics.name = channelName(state.id);
    metrics.txPackets = state.txPackets;
    metrics.maxQueue = state.maxQueue;
    metrics.dropsQueue = state.dropsQueue;
    metrics.dropsLoss = state.dropsLoss;
    metrics.dropsDown = state.dropsDown;
    if (endTime_ > 0.0) {
        metrics.avgQueue = state.queueArea / endTime_;
    }

    const LinkId linkId = state.id.linkId;
    if (linkDowntimeS_.count(linkId) != 0) {
        metrics.downtimeS = linkDowntimeS_.at(linkId);
    }
    if (linkDownTimes_.count(linkId) != 0) {
        metrics.downtimeS += std::max(0.0, endTime_ - linkDownTimes_.at(linkId));
    }

    const Link* link = graph_.getLink(linkId);
    if (link != nullptr && endTime_ > 0.0) {
        const double capacityBits = linkCapacityBits(*link, endTime_);
        if (capacityBits > 0.0) {
            metrics.utilizationPct = 100.0 * state.bitsTransmitted / capacityBits;
        }
    }
    return metrics;
}

WindowMetrics MetricsCollector::summarizeWindow(std::size_t index,
                                                const WindowState& state) const {
    WindowMetrics metrics;
    metrics.start = static_cast<SimTime>(index) * windowSize_;
    metrics.sent = state.sent;
    metrics.delivered = state.delivered;
    metrics.dropped = state.dropped;
    metrics.maxQueue = state.maxQueue;
    metrics.activeFailures = state.activeFailures;
    metrics.lossPct = percentOf(metrics.dropped, metrics.delivered + metrics.dropped);

    double latencyTotal = 0.0;
    for (const double latency : state.latenciesMs) {
        latencyTotal += latency;
    }
    metrics.avgLatencyMs = averageOrZero(latencyTotal, state.latenciesMs.size());
    metrics.jitterMs = state.jitter.average();

    const SimTime duration = std::min(windowSize_, std::max(0.0, endTime_ - metrics.start));
    if (duration > 0.0) {
        metrics.throughputMbps = megabitsPerSecond(state.deliveredBytes, duration);
        metrics.maxUtilizationPct = peakUtilizationPct(state, duration);
    }
    return metrics;
}

double MetricsCollector::peakUtilizationPct(const WindowState& state, SimTime duration) const {
    double peak = 0.0;
    for (const auto& [key, bitsSent] : state.txBitsByChannel) {
        if (channelStates_.count(key) == 0) {
            continue;
        }
        const Link* link = graph_.getLink(channelStates_.at(key).id.linkId);
        if (link == nullptr) {
            continue;
        }
        const double capacityBits = linkCapacityBits(*link, duration);
        if (capacityBits > 0.0) {
            peak = std::max(peak, 100.0 * bitsSent / capacityBits);
        }
    }
    return peak;
}

void MetricsCollector::buildNetworkMetrics() {
    std::size_t deliveredTotal = 0;
    std::size_t droppedTotal = 0;
    double latencyTotal = 0.0;
    for (const FlowMetrics& flow : flows_) {
        deliveredTotal += flow.delivered;
        droppedTotal += flow.dropped;
        latencyTotal += flow.avgLatencyMs * static_cast<double>(flow.delivered);
    }
    network_.reliabilityPct = percentOf(deliveredTotal, deliveredTotal + droppedTotal);
    network_.totalDrops = droppedTotal;
    network_.avgLatencyMs = averageOrZero(latencyTotal, deliveredTotal);

    if (endTime_ > 0.0) {
        double totalDeliveredBytes = 0.0;
        for (const auto& entry : flowStates_) {
            totalDeliveredBytes += entry.second.bytesDelivered;
        }
        network_.avgThroughputMbps = megabitsPerSecond(totalDeliveredBytes, endTime_);
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
