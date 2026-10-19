#include "netmind/simulation/SimulationEngine.h"

#include "netmind/common/Errors.h"
#include "netmind/common/Units.h"
#include "netmind/model/Flow.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Node.h"
#include "netmind/SimObserver.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace netmind {
namespace {

std::uint64_t channelStorageKey(ChannelId id) {
    const auto direction = static_cast<std::uint64_t>(id.dir);

    return (static_cast<std::uint64_t>(id.linkId) << 1) | direction;
}

bool isBeforeFlowStop(SimTime time, SimTime stopTime) {
    const SimTime scale =
        std::max(std::abs(time), std::abs(stopTime));
    const SimTime tolerance =
        4.0 * std::numeric_limits<SimTime>::epsilon() * scale;

    return time < stopTime - tolerance;
}

class CallbackEvent final : public NetworkEvent {
public:
    using Callback = std::function<void(SimulationEngine&)>;

    CallbackEvent(std::string description, Callback callback)
        : description_(std::move(description)),
          callback_(std::move(callback)) {}

    void apply(SimulationEngine& engine) override {
        callback_(engine);
    }

    [[nodiscard]] std::string describe() const override {
        return description_;
    }

private:
    std::string description_;
    Callback callback_;
};

}             

SimulationEngine::SimulationEngine(std::uint64_t seed)
    : rng_(seed) {}

void SimulationEngine::setSeed(std::uint64_t seed) {
    rng_ = Rng(seed);
}

SimulationEngine::~SimulationEngine() = default;

SimTime SimulationEngine::now() const {
    return currentTime_;
}

PacketId SimulationEngine::allocatePacketId() {
    if (nextPacketId_ == std::numeric_limits<PacketId>::max()) {
        throw std::overflow_error("Packet ID counter is exhausted");
    }

    return nextPacketId_++;
}

void SimulationEngine::transmit(
    Packet packet,
    NodeId from,
    NodeId to
) {
    if (graph_ == nullptr) {
        throw std::logic_error(
            "Set the simulation graph before transmitting packets");
    }

    Node* fromNode = graph_->getNode(from);
    Node* toNode = graph_->getNode(to);

    if (fromNode == nullptr || toNode == nullptr) {
        drop(packet, from, DropReason::NO_ROUTE);
        return;
    }

    if (!fromNode->isUp() || !toNode->isUp()) {
        drop(packet, from, DropReason::NODE_DOWN);
        return;
    }

    const Link* link = graph_->linkBetween(from, to);
    if (link == nullptr || !link->isUsable()) {
        drop(packet, from, DropReason::LINK_DOWN);
        return;
    }

    for (SimObserver* observer : observers_) {
        if (fromNode->type() == NodeType::HOST) {
            observer->onSent(currentTime_, packet);
        } else {
            observer->onForward(currentTime_, packet, from, to);
        }
    }

    Channel& channel = channelFor(*link, from, to);

    if (!channel.tryEnqueue(packet, link->queueCapacity())) {
        drop(packet, from, DropReason::QUEUE_FULL);
        return;
    }

    for (SimObserver* observer : observers_) {
        observer->onEnqueue(
            currentTime_,
            packet,
            channel.id(),
            channel.queueSize());
    }

    if (!channel.busy()) {
        beginTransmission(channel);
    }
}

void SimulationEngine::deliver(const Packet& packet, NodeId) {
    for (SimObserver* observer : observers_) {
        observer->onDelivered(currentTime_, packet);
    }
}

void SimulationEngine::drop(
    const Packet& packet,
    NodeId at,
    DropReason reason
) {
    const Node* node =
        graph_ == nullptr ? nullptr : graph_->getNode(at);

    const std::string where =
        node == nullptr ? std::to_string(at) : node->name();

    for (SimObserver* observer : observers_) {
        observer->onDropped(currentTime_, packet, where, reason);
    }
}

Rng& SimulationEngine::rng() {
    return rng_;
}

const Graph& SimulationEngine::graph() const {
    if (graph_ == nullptr) {
        throw std::logic_error(
            "Simulation graph has not been set");
    }

    return *graph_;
}

Graph& SimulationEngine::mutableGraph() {
    if (graph_ == nullptr) {
        throw std::logic_error("Simulation graph has not been set");
    }
    return *graph_;
}

std::optional<SimTime> SimulationEngine::nextEventTime() const {
    return eventQueue_.nextTime();
}

std::vector<ChannelSnapshot> SimulationEngine::channelSnapshots(
    LinkId linkId) const {
    if (graph_ == nullptr) {
        throw std::logic_error("Simulation graph has not been set");
    }

    const Link* link = graph_->getLink(linkId);
    if (link == nullptr) {
        throw TopologyError("Cannot inspect a missing link");
    }

    std::vector<ChannelSnapshot> result;
    for (const auto& entry : channels_) {
        const Channel& channel = *entry.second;
        if (channel.linkId() != linkId) {
            continue;
        }
        result.push_back(ChannelSnapshot{
            channel.id(), channel.from(), channel.to(), channel.busy(),
            channel.queueSize(), link->queueCapacity()});
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id.dir < rhs.id.dir;
    });
    return result;
}

void SimulationEngine::setGraph(std::unique_ptr<Graph> graph) {
    if (graph == nullptr) {
        throw ConfigError("Simulation graph cannot be null");
    }

    if (!eventQueue_.empty()) {
        throw ConfigError(
            "Cannot replace the simulation graph while events are pending");
    }

    channels_.clear();
    graph_ = std::move(graph);
    (void)routingManager_.recomputeAll(*graph_);
}

void SimulationEngine::addObserver(SimObserver& observer) {
    const auto existing =
        std::find(observers_.begin(), observers_.end(), &observer);

    if (existing == observers_.end()) {
        observers_.push_back(&observer);
    }
}

void SimulationEngine::removeObserver(SimObserver& observer) {
    observers_.erase(
        std::remove(observers_.begin(), observers_.end(), &observer),
        observers_.end());
}

void SimulationEngine::scheduleEvent(
    SimTime time,
    std::shared_ptr<NetworkEvent> event
) {
    if (time < currentTime_) {
        throw ConfigError(
            "Cannot schedule an event before the current simulation time");
    }

    eventQueue_.schedule(time, std::move(event));
}

void SimulationEngine::runUntil(SimTime endTime) {
    if (!std::isfinite(endTime) || endTime < currentTime_) {
        throw ConfigError(
            "Run end time must be finite and no earlier than now");
    }

    while (const auto nextTime = eventQueue_.nextTime()) {
        if (*nextTime > endTime) {
            break;
        }

        step();
    }

    currentTime_ = endTime;
}

void SimulationEngine::run() {
    while (!eventQueue_.empty()) {
        step();
    }
}

void SimulationEngine::step() {
    if (eventQueue_.empty()) {
        throw std::logic_error(
            "Cannot step a simulation with no pending events");
    }

    ScheduledEvent scheduled = eventQueue_.pop();

    if (scheduled.time < currentTime_) {
        throw std::logic_error(
            "Event queue returned an event earlier than simulation time");
    }

    currentTime_ = scheduled.time;
    scheduled.event->apply(*this);
}

bool SimulationEngine::finished() const {
    return eventQueue_.empty();
}

std::size_t SimulationEngine::pendingEvents() const {
    return eventQueue_.size();
}

Channel& SimulationEngine::channelFor(
    const Link& link,
    NodeId from,
    NodeId to
) {
    Direction direction;

    if (from == link.a() && to == link.b()) {
        direction = Direction::AtoB;
    } else if (from == link.b() && to == link.a()) {
        direction = Direction::BtoA;
    } else {
        throw TopologyError(
            "Transmission endpoints do not match the link");
    }

    const ChannelId id{link.getId(), direction};
    const std::uint64_t key = channelStorageKey(id);

    const auto existing = channels_.find(key);
    if (existing != channels_.end()) {
        return *existing->second;
    }

    auto channel = std::make_unique<Channel>(id, from, to);
    Channel* channelPointer = channel.get();
    channels_.emplace(key, std::move(channel));

    return *channelPointer;
}

std::uint64_t SimulationEngine::nodeEpoch(NodeId id) const {
    const auto it = nodeEpochs_.find(id);
    return it == nodeEpochs_.end() ? 0 : it->second;
}

void SimulationEngine::beginTransmission(Channel& channel) {
    if (channel.busy() || channel.empty()) {
        return;
    }

    const Link* link =
        graph_->linkBetween(channel.from(), channel.to());

    if (link == nullptr || !link->isUsable()) {
        flushChannel(channel, DropReason::LINK_DOWN);
        return;
    }

    const Node* fromNode = graph_->getNode(channel.from());
    const Node* toNode = graph_->getNode(channel.to());

    if (fromNode == nullptr || toNode == nullptr ||
        !fromNode->isUp() || !toNode->isUp()) {
        flushChannel(channel, DropReason::NODE_DOWN);
        return;
    }

    const std::uint64_t generation =
        channel.beginTransmission();
    const std::uint64_t linkEpoch = link->epoch();
    const std::uint64_t fromEpoch = nodeEpoch(channel.from());
    const std::uint64_t toEpoch = nodeEpoch(channel.to());

    const SimTime transmissionDuration =
        txTimeSec(
            channel.front().sizeBytes,
            link->effectiveBandwidth());
    const SimTime completionTime =
        currentTime_ + transmissionDuration;

    const ChannelId id = channel.id();
    const std::uint64_t key = channelStorageKey(id);

    for (SimObserver* observer : observers_) {
        observer->onTxStart(
            currentTime_,
            channel.front(),
            id,
            completionTime);
    }

    scheduleEvent(
        completionTime,
        std::make_shared<CallbackEvent>(
            "TRANSMIT_COMPLETE",
            [key, generation, linkEpoch, fromEpoch, toEpoch]
            (SimulationEngine& engine) {
                engine.completeTransmission(
                    key,
                    generation,
                    linkEpoch,
                    fromEpoch,
                    toEpoch);
            }));
}

void SimulationEngine::completeTransmission(
    std::uint64_t key,
    std::uint64_t generation,
    std::uint64_t linkEpoch,
    std::uint64_t fromEpoch,
    std::uint64_t toEpoch
) {
    const auto channelIt = channels_.find(key);
    if (channelIt == channels_.end()) {
        return;
    }

    Channel& channel = *channelIt->second;
    const Link* link =
        graph_->linkBetween(channel.from(), channel.to());

    const Node* fromNode = graph_->getNode(channel.from());
    const Node* toNode = graph_->getNode(channel.to());

    if (link == nullptr ||
        !link->isUsable() ||
        link->epoch() != linkEpoch) {
        flushChannel(channel, DropReason::LINK_DOWN);
        return;
    }

    if (fromNode == nullptr || toNode == nullptr ||
        !fromNode->isUp() || !toNode->isUp() ||
        nodeEpoch(channel.from()) != fromEpoch ||
        nodeEpoch(channel.to()) != toEpoch) {
        flushChannel(channel, DropReason::NODE_DOWN);
        return;
    }

    std::optional<Packet> packet =
        channel.completeTransmission(generation);

    if (!packet) {
        return;
    }

    for (SimObserver* observer : observers_) {
        observer->onQueueChanged(
            currentTime_,
            channel.id(),
            channel.queueSize());
    }

    if (rng_.uniform01() < link->effectiveLossRate()) {
        drop(*packet, channel.from(), DropReason::RANDOM_LOSS);
    } else {
        const SimTime arrivalTime =
            currentTime_ + msToSec(link->latencyMs());
        const NodeId from = channel.from();
        const NodeId to = channel.to();
        const LinkId linkId = link->getId();

        scheduleEvent(
            arrivalTime,
            std::make_shared<CallbackEvent>(
                "PACKET_ARRIVE",
                [packet = std::move(*packet),
                 from,
                 to,
                 linkId,
                 linkEpoch,
                 fromEpoch,
                 toEpoch](SimulationEngine& engine) mutable {
                    engine.handlePacketArrival(
                        std::move(packet),
                        from,
                        to,
                        linkId,
                        linkEpoch,
                        fromEpoch,
                        toEpoch);
                }));
    }

    if (!channel.empty()) {
        beginTransmission(channel);
    }
}

void SimulationEngine::handlePacketArrival(
    Packet packet,
    NodeId from,
    NodeId to,
    LinkId linkId,
    std::uint64_t linkEpoch,
    std::uint64_t fromEpoch,
    std::uint64_t toEpoch
) {
    if (graph_ == nullptr) {
        drop(packet, to, DropReason::NODE_DOWN);
        return;
    }

    const Link* link = graph_->linkBetween(from, to);

    if (link == nullptr ||
        !link->isUsable() ||
        link->epoch() != linkEpoch) {
        drop(packet, to, DropReason::LINK_DOWN);
        return;
    }

    Node* node = graph_->getNode(to);
    const Node* fromNode = graph_->getNode(from);

    if (node == nullptr || fromNode == nullptr ||
        !node->isUp() || !fromNode->isUp() ||
        nodeEpoch(from) != fromEpoch ||
        nodeEpoch(to) != toEpoch) {
        drop(packet, to, DropReason::NODE_DOWN);
        return;
    }

    for (SimObserver* observer : observers_) {
        observer->onArrive(currentTime_, packet, to, from);
    }

    node->receivePacket(std::move(packet), linkId, *this);
}

void SimulationEngine::setRouterState(NodeId routerId, bool isUp) {
    if (graph_ == nullptr) {
        throw std::logic_error(
            "Set the simulation graph before changing router state");
    }

    Node* router = graph_->getNode(routerId);
    if (router == nullptr || router->type() != NodeType::ROUTER) {
        throw TopologyError(
            "Cannot change the state of a missing or non-router node");
    }

    if (router->isUp() == isUp) {
        return;
    }

    router->setUp(isUp);
    ++nodeEpochs_[routerId];

    if (!isUp) {
        for (auto& entry : channels_) {
            Channel& channel = *entry.second;
            if (channel.from() == routerId ||
                channel.to() == routerId) {
                flushChannel(channel, DropReason::NODE_DOWN);
            }
        }
    }

    const char* eventType = isUp ? "ROUTER_UP" : "ROUTER_DOWN";
    const char* stateName = isUp ? "up" : "down";

    for (SimObserver* observer : observers_) {
        observer->onNetworkEvent(
            currentTime_,
            eventType,
            std::to_string(routerId),
            std::string("state=") + stateName);
    }

    requestRouteRecompute();
}

void SimulationEngine::flushChannel(
    Channel& channel,
    DropReason reason
) {
    const std::vector<Packet> packets = channel.flush();

    for (SimObserver* observer : observers_) {
        observer->onQueueChanged(
            currentTime_,
            channel.id(),
            channel.queueSize());
    }

    for (const Packet& packet : packets) {
        drop(packet, channel.from(), reason);
    }
}

void SimulationEngine::setLinkState(LinkId linkId, LinkState state) {
    if (graph_ == nullptr) {
        throw std::logic_error(
            "Set the simulation graph before changing link state");
    }

    Link* link = graph_->getLink(linkId);
    if (link == nullptr) {
        throw TopologyError(
            "Cannot change the state of a missing link");
    }

    if (link->state() == state) {
        return;
    }

    link->setState(state);

    if (state == LinkState::DOWN) {
        link->setDegradeFactor(1.0);
        link->setExtraLossRate(0.0);
        ++congestionGenerations_[linkId];

        for (auto& entry : channels_) {
            Channel& channel = *entry.second;
            if (channel.linkId() == linkId) {
                flushChannel(channel, DropReason::LINK_DOWN);
            }
        }
    }

    const char* eventType = "LINK_UP";
    const char* stateName = "up";

    if (state == LinkState::DOWN) {
        eventType = "LINK_DOWN";
        stateName = "down";
    } else if (state == LinkState::DEGRADED) {
        eventType = "LINK_DEGRADED";
        stateName = "degraded";
    }

    for (SimObserver* observer : observers_) {
        observer->onNetworkEvent(
            currentTime_,
            eventType,
            std::to_string(linkId),
            std::string("state=") + stateName);
    }

    requestRouteRecompute();
}

RoutingManager& SimulationEngine::routingManager() {
    return routingManager_;
}

const RoutingManager& SimulationEngine::routingManager() const {
    return routingManager_;
}

void SimulationEngine::recomputeRoutes() {
    if (graph_ == nullptr) {
        throw std::logic_error(
            "Set the simulation graph before recomputing routes");
    }

    const RoutingChangeSummary summary =
        routingManager_.recomputeAll(*graph_);

    if (summary.entriesChanged == 0) {
        return;
    }

    const std::string detail =
        "routers=" + std::to_string(summary.routersProcessed) +
        ";entries_changed=" + std::to_string(summary.entriesChanged);

    for (SimObserver* observer : observers_) {
        observer->onNetworkEvent(
            currentTime_,
            "ROUTE_UPDATE",
            "network",
            detail);
    }
}

void SimulationEngine::addFlow(Flow flow) {
    if (graph_ == nullptr) {
        throw std::logic_error(
            "Set the simulation graph before adding a flow");
    }

    const FlowId flowId = flow.id();
    reservedFlowIds_.erase(flowId);

    if (flows_.find(flowId) != flows_.end()) {
        throw ConfigError("Flow IDs must be unique");
    }

    auto* source =
        dynamic_cast<Host*>(graph_->getNode(flow.src()));
    const auto* destination =
        dynamic_cast<const Host*>(graph_->getNode(flow.dst()));

    if (source == nullptr || destination == nullptr) {
        throw ConfigError(
            "A flow's source and destination must be existing hosts");
    }

    const auto gatewayId = source->gatewayId();
    if (!gatewayId ||
        graph_->linkBetween(flow.src(), *gatewayId) == nullptr) {
        throw ConfigError(
            "A flow's source host must have a gateway connected by a link");
    }

    const FlowId id = flow.id();
    const SimTime startTime = flow.startTime();
    const SimTime stopTime = flow.stopTime();

    flows_.emplace(id, std::move(flow));

    scheduleEvent(
        startTime,
        std::make_shared<CallbackEvent>(
            "FLOW_START",
            [id](SimulationEngine& engine) {
                engine.startFlow(id);
            }));

    scheduleEvent(
        stopTime,
        std::make_shared<CallbackEvent>(
            "FLOW_STOP",
            [id](SimulationEngine& engine) {
                engine.stopFlow(id);
            }));
}

FlowId SimulationEngine::allocateFlowId() {
    for (FlowId candidate = 1;; ++candidate) {
        if (flows_.find(candidate) == flows_.end() &&
            reservedFlowIds_.find(candidate) == reservedFlowIds_.end()) {
            reservedFlowIds_.insert(candidate);
            return candidate;
        }
        if (candidate == std::numeric_limits<FlowId>::max()) {
            throw ConfigError("No flow IDs remain available");
        }
    }
}

bool SimulationEngine::hasFlow(FlowId flowId) const noexcept {
    return flows_.find(flowId) != flows_.end();
}

void SimulationEngine::originatePacket(Packet packet) {
    if (graph_ == nullptr) {
        throw std::logic_error("Set the simulation graph before sending a packet");
    }
    auto* source = dynamic_cast<Host*>(graph_->getNode(packet.srcId));
    if (source == nullptr) {
        throw ConfigError("Packet source must be an existing host");
    }
    if (graph_->getNode(packet.dstId) == nullptr) {
        throw ConfigError("Packet destination does not exist");
    }
    if (packet.id == 0) {
        packet.id = allocatePacketId();
    }
    if (!source->isUp()) {
        drop(packet, source->getId(), DropReason::NODE_DOWN);
        return;
    }
    source->originate(std::move(packet), *this);
}

void SimulationEngine::startFlow(FlowId flowId) {
    const auto flowIt = flows_.find(flowId);
    if (flowIt == flows_.end()) {
        throw std::logic_error("Scheduled flow does not exist");
    }

    const Flow& flow = flowIt->second;

    for (SimObserver* observer : observers_) {
        observer->onNetworkEvent(
            currentTime_,
            "FLOW_START",
            std::to_string(flowId),
            "src=" + std::to_string(flow.src()) +
                ";dst=" + std::to_string(flow.dst()));
    }

    if (isBeforeFlowStop(currentTime_, flow.stopTime())) {
        generateFlowPacket(flowId);
    }
}

void SimulationEngine::generateFlowPacket(FlowId flowId) {
    auto flowIt = flows_.find(flowId);
    if (flowIt == flows_.end()) {
        throw std::logic_error("Scheduled flow does not exist");
    }

    Flow& flow = flowIt->second;

    if (!isBeforeFlowStop(currentTime_, flow.stopTime())) {
        return;
    }

    Packet packet =
        flow.makePacket(currentTime_, allocatePacketId());

    auto* source =
        dynamic_cast<Host*>(graph_->getNode(flow.src()));

    if (source == nullptr) {
        drop(packet, flow.src(), DropReason::NODE_DOWN);
    } else if (!source->isUp()) {
        drop(packet, flow.src(), DropReason::NODE_DOWN);
    } else {
        source->originate(std::move(packet), *this);
    }

    const SimTime interval =
        flow.pattern().nextInterval(rng_);

    if (!std::isfinite(interval) || interval <= 0.0) {
        throw ConfigError(
            "Traffic pattern must produce a finite positive interval");
    }

    const SimTime nextTime = currentTime_ + interval;

    if (isBeforeFlowStop(nextTime, flow.stopTime())) {
        scheduleEvent(
            nextTime,
            std::make_shared<CallbackEvent>(
                "GENERATE_PACKET",
                [flowId](SimulationEngine& engine) {
                    engine.generateFlowPacket(flowId);
                }));
    }
}

void SimulationEngine::stopFlow(FlowId flowId) {
    const auto flowIt = flows_.find(flowId);
    if (flowIt == flows_.end()) {
        throw std::logic_error("Scheduled flow does not exist");
    }

    for (SimObserver* observer : observers_) {
        observer->onNetworkEvent(
            currentTime_,
            "FLOW_STOP",
            std::to_string(flowId),
            "state=stopped");
    }
}

void SimulationEngine::requestRouteRecompute() {
    const SimTime delay = routingManager_.reconvergenceDelay();

    if (delay == 0.0) {
        recomputeRoutes();
        return;
    }

    scheduleEvent(
        currentTime_ + delay,
        std::make_shared<CallbackEvent>(
            "ROUTE_RECOMPUTE",
            [](SimulationEngine& engine) {
                engine.recomputeRoutes();
            }));
}

void SimulationEngine::applyCongestion(
    LinkId linkId,
    double bandwidthMultiplier,
    double extraLossRate,
    SimTime duration
) {
    if (graph_ == nullptr) {
        throw std::logic_error(
            "Set the simulation graph before applying congestion");
    }

    Link* link = graph_->getLink(linkId);
    if (link == nullptr) {
        throw TopologyError(
            "Cannot congest a missing link");
    }

    if (link->state() != LinkState::UP) {
        throw ConfigError(
            "Congestion can only be applied to an up link");
    }

    if (!std::isfinite(bandwidthMultiplier) ||
        bandwidthMultiplier <= 0.0 ||
        bandwidthMultiplier > 1.0) {
        throw ConfigError(
            "Congestion bandwidth multiplier must be in (0, 1]");
    }

    if (!std::isfinite(extraLossRate) ||
        extraLossRate < 0.0 ||
        link->lossRate() + extraLossRate > 1.0) {
        throw ConfigError(
            "Congestion extra loss must keep total link loss in [0, 1]");
    }

    if (!std::isfinite(duration) || duration <= 0.0) {
        throw ConfigError(
            "Congestion duration must be finite and positive");
    }

    link->setDegradeFactor(bandwidthMultiplier);
    link->setExtraLossRate(extraLossRate);

    const std::uint64_t generation =
        ++congestionGenerations_[linkId];

    setLinkState(linkId, LinkState::DEGRADED);

    scheduleEvent(
        currentTime_ + duration,
        std::make_shared<CallbackEvent>(
            "CONGESTION_END",
            [linkId, generation](SimulationEngine& engine) {
                engine.endCongestion(linkId, generation);
            }));
}

void SimulationEngine::endCongestion(
    LinkId linkId,
    std::uint64_t generation
) {
    const auto generationIt =
        congestionGenerations_.find(linkId);

    if (generationIt == congestionGenerations_.end() ||
        generationIt->second != generation) {
        return;
    }

    Link* link = graph_->getLink(linkId);
    if (link == nullptr || link->state() != LinkState::DEGRADED) {
        return;
    }

    link->setDegradeFactor(1.0);
    link->setExtraLossRate(0.0);
    setLinkState(linkId, LinkState::UP);
}

}
