#pragma once

#include "netmind/common/Rng.h"
#include "netmind/common/Types.h"
#include "netmind/graph/Graph.h"
#include "netmind/simulation/Channel.h"
#include "netmind/simulation/EventQueue.h"
#include "netmind/simulation/NetworkEvent.h"
#include "netmind/simulation/SimContext.h"
#include "netmind/routing/RoutingManager.h"
#include "netmind/model/Flow.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unordered_map>

namespace netmind {

class Link;
class SimObserver;

struct ChannelSnapshot {
    ChannelId id;
    NodeId from{};
    NodeId to{};
    bool busy{};
    std::size_t queuedPackets{};
    std::size_t capacity{};
};

class SimulationEngine : public SimContext {
public:
    explicit SimulationEngine(std::uint64_t seed);
    ~SimulationEngine() override;

    SimulationEngine(const SimulationEngine&) = delete;
    SimulationEngine& operator=(const SimulationEngine&) = delete;

    [[nodiscard]] SimTime now() const override;
    [[nodiscard]] PacketId allocatePacketId() override;

    void transmit(Packet pkt, NodeId from, NodeId to) override;
    void deliver(const Packet& pkt, NodeId at) override;
    void drop(const Packet& pkt, NodeId at, DropReason why) override;

    [[nodiscard]] Rng& rng() override;
    [[nodiscard]] const Graph& graph() const override;
    [[nodiscard]] Graph& mutableGraph();

    [[nodiscard]] RoutingManager& routingManager();
    [[nodiscard]] const RoutingManager& routingManager() const;

    void addFlow(Flow flow);
    [[nodiscard]] FlowId allocateFlowId();
    [[nodiscard]] bool hasFlow(FlowId flowId) const noexcept;
    void originatePacket(Packet packet);
    void setSeed(std::uint64_t seed);
    
    void setGraph(std::unique_ptr<Graph> graph);
    void setLinkState(LinkId linkId, LinkState state);
    void addObserver(SimObserver& observer);
    void removeObserver(SimObserver& observer);

    void setRouterState(NodeId routerId, bool isUp);

    void scheduleEvent(
        SimTime time,
        std::shared_ptr<NetworkEvent> event
    );

    void runUntil(SimTime endTime);
    void run();
    void step();

    void applyCongestion(
        LinkId linkId,
        double bandwidthMultiplier,
        double extraLossRate,
        SimTime duration
    );

    [[nodiscard]] bool finished() const;
    [[nodiscard]] std::size_t pendingEvents() const;
    [[nodiscard]] std::optional<SimTime> nextEventTime() const;
    [[nodiscard]] std::vector<ChannelSnapshot> channelSnapshots(
        LinkId linkId) const;

private:
    [[nodiscard]] Channel& channelFor(
        const Link& link,
        NodeId from,
        NodeId to
    );

    void beginTransmission(Channel& channel);
    void completeTransmission(
        std::uint64_t channelKey,
        std::uint64_t generation,
        std::uint64_t linkEpoch,
        std::uint64_t fromEpoch,
        std::uint64_t toEpoch
    );
    
    void handlePacketArrival(
        Packet packet,
        NodeId from,
        NodeId to,
        LinkId linkId,
        std::uint64_t linkEpoch,
        std::uint64_t fromEpoch,
        std::uint64_t toEpoch
    );

    void flushChannel(Channel& channel, DropReason reason);

    void recomputeRoutes();
    void requestRouteRecompute();

    void startFlow(FlowId flowId);
    void generateFlowPacket(FlowId flowId);
    void stopFlow(FlowId flowId);

    void endCongestion(LinkId linkId, std::uint64_t generation);

    SimTime currentTime_{0.0};
    Rng rng_;
    std::unique_ptr<Graph> graph_;
    EventQueue eventQueue_;
    std::vector<SimObserver*> observers_;
    PacketId nextPacketId_{1};
    RoutingManager routingManager_;
    std::unordered_map<std::uint64_t, std::unique_ptr<Channel>> channels_;
    std::unordered_map<NodeId, std::uint64_t> nodeEpochs_;
    std::unordered_map<FlowId, Flow> flows_;
    std::unordered_set<FlowId> reservedFlowIds_;
    [[nodiscard]] std::uint64_t nodeEpoch(NodeId id) const;
    std::unordered_map<LinkId, std::uint64_t> congestionGenerations_;
};

}
